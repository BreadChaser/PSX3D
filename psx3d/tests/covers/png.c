#include <assert.h>
#include <math.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../source/covers_png.h"
#include "../../source/covers_pack.h"
static int limited;
void *__real_malloc(size_t n);
void *__wrap_malloc(size_t n) { return limited && n > 512*1024 ? NULL : __real_malloc(n); }
static double to_lin(int c)
{
	double v = c / 255.0;
	return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}
static int to_srgb(double l)
{
	double v = l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1 / 2.4) - 0.055;
	return (int)(v * 255 + 0.5);
}
// area average in linear light in double precision, transparent pixels flattened
// onto the card colour: what the decoder must match to within its 12 bit table
static uint32_t *reference(const char *path, int max, int *ow, int *oh)
{
	png_image img = { .version = PNG_IMAGE_VERSION };
	assert(png_image_begin_read_from_file(&img, path));
	img.format = PNG_FORMAT_RGBA;
	unsigned char *rgba = malloc(PNG_IMAGE_SIZE(img));
	assert(rgba && png_image_finish_read(&img, NULL, rgba, 0, NULL));
	covers_fit(img.width, img.height, max, ow, oh);
	uint32_t *out = malloc(*ow * *oh * 4);
	for (int y=0;y<*oh;y++) for(int x=0;x<*ow;x++) {
		unsigned x0=x*img.width / *ow, x1=(x+1)*img.width / *ow;
		unsigned y0=y*img.height / *oh, y1=(y+1)*img.height / *oh;
		if(x1<=x0)x1=x0+1;
		if(y1<=y0)y1=y0+1;
		double sum[3]={0};
		unsigned n=0;
		for(unsigned yy=y0;yy<y1;yy++) for(unsigned xx=x0;xx<x1;xx++) {
			unsigned char *p=rgba+(yy*img.width+xx)*4;
			unsigned a=p[3], c[3]={p[0],p[1],p[2]}, bg[3]={COVER_BG_R,COVER_BG_G,COVER_BG_B};
			for(int k=0;k<3;k++) {
				if(a!=255) c[k]=(c[k]*a+bg[k]*(255-a)+127)/255;
				sum[k]+=to_lin(c[k]);
			}
			n++;
		}
		out[y * *ow + x]=(uint32_t)to_srgb(sum[0]/n)<<24|(uint32_t)to_srgb(sum[1]/n)<<16|(uint32_t)to_srgb(sum[2]/n)<<8|255;
	}
	free(rgba);png_image_free(&img);return out;
}
static void same(const uint32_t *a, const uint32_t *b, int n, const char *name)
{
	for (int i=0;i<n;i++) for (int s=24;s>=0;s-=8) {
		int d = (int)(a[i]>>s&255) - (int)(b[i]>>s&255);
		if (d<-2 || d>2) { fprintf(stderr,"DIFF %s pixel %d channel %d: %u vs %u\n",name,i,s,a[i]>>s&255,b[i]>>s&255); exit(1); }
	}
}
int main(int argc, char **argv)
{
	static const int maxes[] = { 256, 64 };
	for(int i=1;i<argc;i++) for (unsigned m=0;m<sizeof(maxes)/sizeof(*maxes);m++) {
		int max=maxes[m], ow, oh, dw, dh, mw, mh;
		FILE *f=fopen(argv[i],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
		unsigned char *data=malloc(n);assert(fread(data,1,n,f)==(size_t)n);fclose(f);
		uint32_t *ref=reference(argv[i],max,&ow,&oh);
		assert(ow<=max && oh<=max && ow>=1 && oh>=1);
		limited=1;
		assert(!malloc(1024*1024));
		uint32_t *disk=covers_png_decode(NULL,0,argv[i],max,&dw,&dh),*mem=covers_png_decode(data,n,NULL,max,&mw,&mh);
		assert(disk && mem);
		assert(dw==ow && dh==oh && mw==ow && mh==oh);
		same(ref,disk,ow*oh,argv[i]);
		assert(!memcmp(disk,mem,ow*oh*4));
		assert(!covers_png_decode(data,n/2,NULL,max,&mw,&mh));
		limited=0;
		free(data);free(ref);free(disk);free(mem);
		printf("PASS %s max %d -> %dx%d\n",argv[i],max,ow,oh);
	}
	int w,h;
	assert(!covers_png_decode("broken",6,NULL,128,&w,&h));
	assert(!covers_png_decode(NULL,0,"missing-file",128,&w,&h));
}

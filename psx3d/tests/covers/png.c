#include <assert.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../source/covers_png.h"
static int limited;
void *__real_malloc(size_t n);
void *__wrap_malloc(size_t n) { return limited && n > 128*1024 ? NULL : __real_malloc(n); }
static uint32_t *reference(const char *path, int size)
{
	png_image img = { .version = PNG_IMAGE_VERSION };
	assert(png_image_begin_read_from_file(&img, path));
	img.format = PNG_FORMAT_RGBA;
	unsigned char *rgba = malloc(PNG_IMAGE_SIZE(img));
	assert(rgba && png_image_finish_read(&img, NULL, rgba, 0, NULL));
	uint32_t *out = malloc(size * size * 4);
	for (int y=0;y<size;y++) for(int x=0;x<size;x++) {
		unsigned x0=x*img.width/size, x1=(x+1)*img.width/size;
		unsigned y0=y*img.height/size, y1=(y+1)*img.height/size;
		if(x1<=x0)x1=x0+1;
		if(y1<=y0)y1=y0+1;
		unsigned r=0,g=0,b=0,a=0,n=0;
		for(unsigned yy=y0;yy<y1;yy++) for(unsigned xx=x0;xx<x1;xx++) {
			unsigned char *p=rgba+(yy*img.width+xx)*4;
			r+=p[0];g+=p[1];b+=p[2];a+=p[3];n++;
		}
		out[y*size+x]=((r/n)<<24)|((g/n)<<16)|((b/n)<<8)|(a/n);
	}
	free(rgba);png_image_free(&img);return out;
}
int main(int argc, char **argv)
{
	for(int i=1;i<argc;i++) {
		FILE *f=fopen(argv[i],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
		unsigned char *data=malloc(n);assert(fread(data,1,n,f)==(size_t)n);fclose(f);
		uint32_t *ref=reference(argv[i],128);
		limited=1;
		assert(!malloc(1024*1024));
		uint32_t *disk=covers_png_decode(NULL,0,argv[i],128),*mem=covers_png_decode(data,n,NULL,128);
		assert(disk && mem);
		if(memcmp(ref,disk,128*128*4)) {fprintf(stderr,"DIFF %s\n",argv[i]);return 1;}
		assert(!memcmp(ref,mem,128*128*4));
		assert(!covers_png_decode(data,n/2,NULL,128));
		limited=0;
		free(data);free(ref);free(disk);free(mem);
		printf("PASS %s\n",argv[i]);
	}
	assert(!covers_png_decode("broken",6,NULL,128));
	assert(!covers_png_decode(NULL,0,"missing-file",128));
}

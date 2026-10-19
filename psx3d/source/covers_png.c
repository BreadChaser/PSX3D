// Decode one scanline at a time: a typical 512x512 cover previously needed a
// contiguous 1 MiB RGBA buffer, in addition to curl/TLS and existing covers.
#include "covers_png.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const unsigned char *data;
	size_t len, pos;
	FILE *file;
	png_structp png;
	png_infop info;
	unsigned char *row;
	uint32_t *out;
} decode_t;

static void read_memory(png_structp png, png_bytep out, png_size_t n)
{
	decode_t *d = png_get_io_ptr(png);
	if (n > d->len - d->pos) png_error(png, "short cover PNG");
	memcpy(out, d->data + d->pos, n);
	d->pos += n;
}

// Preserve libpng's simplified color conversion for 16-bit and Adam7 user
// PNGs. Database covers use the 8-bit noninterlaced streaming path.
static uint32_t *full_image(const void *data, size_t len, const char *file, int size)
{
	png_image img = { .version = PNG_IMAGE_VERSION };
	if (!(file ? png_image_begin_read_from_file(&img, file) :
	             png_image_begin_read_from_memory(&img, data, len))) return NULL;
	img.format = PNG_FORMAT_RGBA;
	unsigned char *rgba = malloc(PNG_IMAGE_SIZE(img));
	uint32_t *out = NULL;
	if (rgba && png_image_finish_read(&img, NULL, rgba, 0, NULL)) {
		out = malloc(size * size * sizeof(*out));
		if (out) for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
			unsigned x0 = x * img.width / size, x1 = (x + 1) * img.width / size;
			unsigned y0 = y * img.height / size, y1 = (y + 1) * img.height / size;
			if (x1 <= x0) x1 = x0 + 1;
			if (y1 <= y0) y1 = y0 + 1;
			unsigned r=0, g=0, b=0, a=0, n=0;
			for (unsigned yy=y0; yy<y1; yy++) for (unsigned xx=x0; xx<x1; xx++) {
				const unsigned char *p=rgba+(yy*img.width+xx)*4;
				r+=p[0]; g+=p[1]; b+=p[2]; a+=p[3]; n++;
			}
			out[y*size+x]=((r/n)<<24)|((g/n)<<16)|((b/n)<<8)|(a/n);
		}
	}
	free(rgba);
	png_image_free(&img);
	return out;
}

uint32_t *covers_png_decode(const void *data, size_t len, const char *file, int size)
{
	decode_t *d = calloc(1, sizeof(*d));
	if (!d) return NULL;
	d->data = data; d->len = len;
	if (file && !(d->file = fopen(file, "rb"))) { free(d); return NULL; }
	d->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!d->png) goto done;
	d->info = png_create_info_struct(d->png);
	if (!d->info) goto done;
	if (setjmp(png_jmpbuf(d->png))) { free(d->out); d->out = NULL; goto done; }
	png_set_user_limits(d->png, 4096, 4096);
	if (d->file) png_init_io(d->png, d->file);
	else png_set_read_fn(d->png, d, read_memory);
	png_read_info(d->png, d->info);
	unsigned w = png_get_image_width(d->png, d->info), h = png_get_image_height(d->png, d->info);
	if (!w || !h || size <= 0 || size > 512) goto done;
	if (png_get_interlace_type(d->png, d->info) != PNG_INTERLACE_NONE ||
	    png_get_bit_depth(d->png, d->info) == 16) {
		d->out = full_image(data, len, file, size);
		goto done;
	}
	png_set_alpha_mode(d->png, PNG_ALPHA_PNG, PNG_DEFAULT_sRGB);
	int depth = png_get_bit_depth(d->png, d->info), color = png_get_color_type(d->png, d->info);
	if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(d->png);
	if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(d->png);
	int transparency = png_get_valid(d->png, d->info, PNG_INFO_tRNS);
	if (transparency) png_set_tRNS_to_alpha(d->png);
	if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(d->png);
	if (!(color & PNG_COLOR_MASK_ALPHA) && !transparency) png_set_add_alpha(d->png, 255, PNG_FILLER_AFTER);
	png_read_update_info(d->png, d->info);
	d->row = malloc(png_get_rowbytes(d->png, d->info));
	d->out = malloc(size * size * sizeof(*d->out));
	if (!d->row || !d->out) { free(d->out); d->out = NULL; goto done; }
	int row = -1;
	for (int y = 0; y < size; y++) {
		unsigned sums[512][4] = {{0}};
		unsigned y0 = y * h / size, y1 = (y + 1) * h / size;
		if (y1 <= y0) y1 = y0 + 1;
		for (unsigned yy = y0; yy < y1; yy++) {
			while (row < (int)yy) { png_read_row(d->png, d->row, NULL); row++; }
			for (int x = 0; x < size; x++) {
				unsigned x0 = x * w / size, x1 = (x + 1) * w / size;
				if (x1 <= x0) x1 = x0 + 1;
				for (unsigned xx = x0; xx < x1; xx++) for (int c = 0; c < 4; c++)
					sums[x][c] += d->row[xx * 4 + c];
			}
		}
		for (int x = 0; x < size; x++) {
			unsigned x0 = x * w / size, x1 = (x + 1) * w / size;
			if (x1 <= x0) x1 = x0 + 1;
			unsigned n = (x1 - x0) * (y1 - y0);
			d->out[y * size + x] = ((sums[x][0]/n)<<24)|((sums[x][1]/n)<<16)|((sums[x][2]/n)<<8)|(sums[x][3]/n);
		}
	}
	png_read_end(d->png, NULL);
done:
	// Error cleanup is safe across libpng longjmp: all owned pointers live in d.
	if (d->png) png_destroy_read_struct(&d->png, d->info ? &d->info : NULL, NULL);
	if (d->file) fclose(d->file);
	free(d->row);
	uint32_t *out = d->out;
	free(d);
	return out;
}

// Decode one scanline at a time: a typical 512x512 cover previously needed a
// contiguous 1 MiB RGBA buffer, in addition to curl/TLS and existing covers.
#include "covers_png.h"
#include "covers_pack.h"
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

// Add one source row, 8 bit RGBA, to the sums of the ow output columns it
// feeds (linear light, 12 bit).
static void accumulate(unsigned (*sums)[3], const unsigned char *row, unsigned w, int ow)
{
	for (int x = 0; x < ow; x++) {
		unsigned x0 = (unsigned)x * w / ow, x1 = (unsigned)(x + 1) * w / ow;
		if (x1 <= x0) x1 = x0 + 1;
		for (unsigned xx = x0; xx < x1; xx++) {
			const unsigned char *p = row + xx * 4;
			unsigned r = p[0], g = p[1], b = p[2], a = p[3];
			if (a != 255) {
				r = (r * a + COVER_BG_R * (255 - a) + 127) / 255;
				g = (g * a + COVER_BG_G * (255 - a) + 127) / 255;
				b = (b * a + COVER_BG_B * (255 - a) + 127) / 255;
			}
			sums[x][0] += covers_lin[r];
			sums[x][1] += covers_lin[g];
			sums[x][2] += covers_lin[b];
		}
	}
}

static uint32_t average(const unsigned *sum, unsigned n)
{
	unsigned r = (sum[0] + n / 2) / n, g = (sum[1] + n / 2) / n, b = (sum[2] + n / 2) / n;
	return (uint32_t)covers_srgb[r > 4095 ? 4095 : r] << 24 | (uint32_t)covers_srgb[g > 4095 ? 4095 : g] << 16 |
	       (uint32_t)covers_srgb[b > 4095 ? 4095 : b] << 8 | 0xff;
}

// Preserve libpng's simplified color conversion for 16-bit and Adam7 user
// PNGs. Database covers use the 8-bit noninterlaced streaming path.
static uint32_t *full_image(const void *data, size_t len, const char *file, int ow, int oh)
{
	png_image img = { .version = PNG_IMAGE_VERSION };
	if (!(file ? png_image_begin_read_from_file(&img, file) :
	             png_image_begin_read_from_memory(&img, data, len))) return NULL;
	img.format = PNG_FORMAT_RGBA;
	unsigned char *rgba = malloc(PNG_IMAGE_SIZE(img));
	uint32_t *out = NULL;
	if (rgba && png_image_finish_read(&img, NULL, rgba, 0, NULL)) {
		out = malloc(ow * oh * sizeof(*out));
		if (out) for (int y = 0; y < oh; y++) {
			unsigned sums[512][3] = {{0}};
			unsigned y0 = (unsigned)y * img.height / oh, y1 = (unsigned)(y + 1) * img.height / oh;
			if (y1 <= y0) y1 = y0 + 1;
			for (unsigned yy = y0; yy < y1; yy++)
				accumulate(sums, rgba + (size_t)yy * img.width * 4, img.width, ow);
			for (int x = 0; x < ow; x++) {
				unsigned x0 = (unsigned)x * img.width / ow, x1 = (unsigned)(x + 1) * img.width / ow;
				if (x1 <= x0) x1 = x0 + 1;
				out[y * ow + x] = average(sums[x], (x1 - x0) * (y1 - y0));
			}
		}
	}
	free(rgba);
	png_image_free(&img);
	return out;
}

uint32_t *covers_png_decode(const void *data, size_t len, const char *file, int max, int *ow_out, int *oh_out)
{
	covers_gamma_init();
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
	if (!w || !h || max <= 0 || max > 512) goto done;
	int ow, oh;
	covers_fit(w, h, max, &ow, &oh);
	*ow_out = ow;
	*oh_out = oh;
	if (png_get_interlace_type(d->png, d->info) != PNG_INTERLACE_NONE ||
	    png_get_bit_depth(d->png, d->info) == 16) {
		d->out = full_image(data, len, file, ow, oh);
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
	d->out = malloc(ow * oh * sizeof(*d->out));
	if (!d->row || !d->out) { free(d->out); d->out = NULL; goto done; }
	int row = -1;
	for (int y = 0; y < oh; y++) {
		unsigned sums[512][3] = {{0}};
		unsigned y0 = (unsigned)y * h / oh, y1 = (unsigned)(y + 1) * h / oh;
		if (y1 <= y0) y1 = y0 + 1;
		for (unsigned yy = y0; yy < y1; yy++) {
			while (row < (int)yy) { png_read_row(d->png, d->row, NULL); row++; }
			accumulate(sums, d->row, w, ow);
		}
		for (int x = 0; x < ow; x++) {
			unsigned x0 = (unsigned)x * w / ow, x1 = (unsigned)(x + 1) * w / ow;
			if (x1 <= x0) x1 = x0 + 1;
			d->out[y * ow + x] = average(sums[x], (x1 - x0) * (y1 - y0));
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

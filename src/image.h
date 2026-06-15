/* image.h - common in-memory frame format for imgcli.
 *
 * Like ffmpeg normalizes everything to an AVFrame in a chosen pixel format,
 * imgcli normalizes every decoded image to packed 8-bit RGBA, row-major,
 * 4 bytes per pixel. Every filter consumes and produces this one format, so
 * filters never have to care what was on disk.
 */
#ifndef IMGCLI_IMAGE_H
#define IMGCLI_IMAGE_H

#include <stddef.h>

typedef struct {
    int w, h;             /* dimensions in pixels                       */
    unsigned char *px;    /* w*h*4 bytes, RGBA RGBA ..., row-major      */
} Image;

/* Safety limits (defense-in-depth against decompression bombs and integer-
 * overflow -> undersized allocation -> OOB write; see SECURITY.md). EVERY
 * frame allocation in the program is bounded by these. Override at build time
 * with -DIMG_MAX_DIM=.. / -DIMG_MAX_PIXELS=.. if you knowingly need more. */
#ifndef IMG_MAX_DIM
#define IMG_MAX_DIM     16384u                  /* max width or height (px)      */
#endif
#ifndef IMG_MAX_PIXELS
#define IMG_MAX_PIXELS  (64u * 1024u * 1024u)   /* max W*H; 64Mpx = 256 MiB RGBA */
#endif

/* 1 if (w,h) are positive and within IMG_MAX_DIM / IMG_MAX_PIXELS, else 0. */
int img_dims_ok(long w, long h);

Image *img_alloc(int w, int h);          /* zero-filled (transparent black) */
Image *img_clone(const Image *src);
void   img_free(Image *im);

/* Decode any format stb understands (PNG/JPEG/BMP/TGA/GIF/PSD/HDR/PIC/PNM).
 * Dimensions are validated against the safety limits BEFORE the pixel data is
 * decoded, so a crafted file cannot trigger a giant allocation. */
Image *img_load(const char *path, char **err);

/* Decode from an in-memory buffer (used by the fuzz harness; same limits). */
Image *img_load_mem(const unsigned char *buf, int len, char **err);

/* Encode by file extension: png, jpg/jpeg, bmp, tga, ppm.
 * jpeg_quality is 1..100 (used only for jpg). Returns 1 on success. */
int    img_save(const char *path, const Image *im, int jpeg_quality, char **err);

/* Bounds-checked pixel address (no clamping; caller guarantees range). */
static inline unsigned char *img_at(const Image *im, int x, int y) {
    return im->px + ((size_t)y * im->w + x) * 4;
}

#endif /* IMGCLI_IMAGE_H */

/* image.c - decode/encode glue around the vendored stb single-header libs. */
#include "image.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The stb implementations live here, in exactly one translation unit. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
/* First-line decompression-bomb guard: make stb itself reject any axis larger
 * than our cap during header parsing, before it allocates pixel memory. */
#define STBI_MAX_DIMENSIONS ((int)IMG_MAX_DIM)
#include "../third_party/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

static char *dupstr(const char *s) {
    if (!s) s = "";
    char *d = (char *)malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}

int img_dims_ok(long w, long h) {
    if (w <= 0 || h <= 0) return 0;
    if (w > (long)IMG_MAX_DIM || h > (long)IMG_MAX_DIM) return 0;
    /* (long long) product cannot overflow for values <= IMG_MAX_DIM (16384). */
    if ((long long)w * (long long)h > (long long)IMG_MAX_PIXELS) return 0;
    return 1;
}

Image *img_alloc(int w, int h) {
    /* Central choke point: every generated/filtered frame is allocated here,
     * so enforcing the safety limits here covers the whole program. */
    if (!img_dims_ok(w, h)) return NULL;
    Image *im = (Image *)malloc(sizeof *im);
    if (!im) return NULL;
    im->w = w;
    im->h = h;
    /* w*h is bounded by IMG_MAX_PIXELS, so w*h*4 cannot overflow size_t on any
     * platform with >=32-bit size_t; calloc also re-checks the *4. */
    im->px = (unsigned char *)calloc((size_t)w * h, 4);
    if (!im->px) { free(im); return NULL; }
    return im;
}

Image *img_clone(const Image *src) {
    if (!src) return NULL;
    Image *im = img_alloc(src->w, src->h);
    if (!im) return NULL;
    memcpy(im->px, src->px, (size_t)src->w * src->h * 4);
    return im;
}

void img_free(Image *im) {
    if (im) { free(im->px); free(im); }
}

/* Wrap a stb-decoded RGBA buffer in an Image, validating dimensions. Takes
 * ownership of `data` (frees it on rejection). NULL `data` => decode failed. */
static Image *wrap_decoded(unsigned char *data, int w, int h, char **err) {
    if (!data) {
        if (err) { const char *r = stbi_failure_reason(); *err = dupstr(r ? r : "decode failed"); }
        return NULL;
    }
    if (!img_dims_ok(w, h)) {
        stbi_image_free(data);
        if (err) *err = dupstr("image dimensions exceed safety limit (see SECURITY.md)");
        return NULL;
    }
    Image *im = (Image *)malloc(sizeof *im);
    if (!im) { stbi_image_free(data); if (err) *err = dupstr("out of memory"); return NULL; }
    im->w = w;
    im->h = h;
    im->px = data; /* stb default allocator is malloc/free, so img_free() is safe */
    return im;
}

Image *img_load(const char *path, char **err) {
    int w, h, n;
    /* Header-only read first: learn the dimensions WITHOUT decoding pixels, so a
     * decompression bomb (small file claiming huge size) is rejected before any
     * large allocation. This is the key mitigation against the stb GIF/TGA/PNM
     * over-allocation class of issues (see SECURITY.md). */
    if (!stbi_info(path, &w, &h, &n)) {
        if (err) { const char *r = stbi_failure_reason(); *err = dupstr(r ? r : "decode failed"); }
        return NULL;
    }
    if (!img_dims_ok(w, h)) {
        if (err) *err = dupstr("image dimensions exceed safety limit (see SECURITY.md)");
        return NULL;
    }
    /* Force 4 channels so every frame is uniform RGBA regardless of source. */
    return wrap_decoded(stbi_load(path, &w, &h, &n, 4), w, h, err);
}

Image *img_load_mem(const unsigned char *buf, int len, char **err) {
    int w, h, n;
    if (!buf || len <= 0) { if (err) *err = dupstr("empty input"); return NULL; }
    if (!stbi_info_from_memory(buf, len, &w, &h, &n)) {
        if (err) { const char *r = stbi_failure_reason(); *err = dupstr(r ? r : "decode failed"); }
        return NULL;
    }
    if (!img_dims_ok(w, h)) {
        if (err) *err = dupstr("image dimensions exceed safety limit");
        return NULL;
    }
    return wrap_decoded(stbi_load_from_memory(buf, len, &w, &h, &n, 4), w, h, err);
}

/* Flatten RGBA over an opaque background into a fresh RGB buffer (for jpg/bmp
 * which have no alpha channel). Background is black, matching ffmpeg's default. */
static unsigned char *to_rgb(const Image *im) {
    size_t n = (size_t)im->w * im->h;
    unsigned char *rgb = (unsigned char *)malloc(n * 3);
    if (!rgb) return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned a = im->px[i * 4 + 3];
        for (int c = 0; c < 3; c++)
            rgb[i * 3 + c] = (unsigned char)((im->px[i * 4 + c] * a) / 255);
    }
    return rgb;
}

/* Minimal from-scratch P6 (binary) PPM writer - no library involved. */
static int write_ppm(const char *path, const Image *im) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", im->w, im->h);
    unsigned char *rgb = to_rgb(im);
    int ok = 0;
    if (rgb) {
        ok = fwrite(rgb, 3, (size_t)im->w * im->h, f) == (size_t)im->w * im->h;
        free(rgb);
    }
    fclose(f);
    return ok;
}

int img_save(const char *path, const Image *im, int jpeg_quality, char **err) {
    const char *dot = strrchr(path, '.');
    char ext[16] = {0};
    if (dot) {
        size_t i = 0;
        for (const char *p = dot + 1; *p && i < sizeof(ext) - 1; p++)
            ext[i++] = (char)tolower((unsigned char)*p);
    }

    int ok = 0;
    if (!strcmp(ext, "png")) {
        ok = stbi_write_png(path, im->w, im->h, 4, im->px, im->w * 4);
    } else if (!strcmp(ext, "jpg") || !strcmp(ext, "jpeg")) {
        unsigned char *rgb = to_rgb(im);
        if (rgb) {
            int q = jpeg_quality < 1 ? 1 : (jpeg_quality > 100 ? 100 : jpeg_quality);
            ok = stbi_write_jpg(path, im->w, im->h, 3, rgb, q);
            free(rgb);
        }
    } else if (!strcmp(ext, "bmp")) {
        unsigned char *rgb = to_rgb(im);
        if (rgb) { ok = stbi_write_bmp(path, im->w, im->h, 3, rgb); free(rgb); }
    } else if (!strcmp(ext, "tga")) {
        ok = stbi_write_tga(path, im->w, im->h, 4, im->px);
    } else if (!strcmp(ext, "ppm")) {
        ok = write_ppm(path, im);
    } else {
        if (err) *err = dupstr(ext[0] ? "unsupported output extension (use png/jpg/bmp/tga/ppm)"
                                       : "output has no extension");
        return 0;
    }

    if (!ok && err) *err = dupstr("encode/write failed");
    return ok;
}

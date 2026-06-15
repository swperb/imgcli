/* filters.c - filter catalogue + filtergraph parser/runner.
 *
 * Calling convention: every filter receives the current frame `in` and the
 * colon-split argument tokens. A filter MAY modify `in` in place and return it
 * (point filters do this), OR allocate and return a brand new frame (geometry
 * filters do this). run_filtergraph() frees the previous frame only when the
 * returned pointer differs, so both styles are safe. On error a filter returns
 * NULL and sets *err to a malloc'd message.
 */
#include "filters.h"
#include "util.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ utils */

static char *dupstr(const char *s) {
    if (!s) s = "";
    char *d = (char *)malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}
static void seterr(char **err, const char *msg) { if (err) *err = dupstr(msg); }

static unsigned char clampb(double v) {
    if (v <= 0) return 0;
    if (v >= 255) return 255;
    return (unsigned char)(v + 0.5);
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* nearest sample with edge clamping */
static const unsigned char *sample_clamp(const Image *im, int x, int y) {
    x = clampi(x, 0, im->w - 1);
    y = clampi(y, 0, im->h - 1);
    return img_at((Image *)im, x, y);
}

/* bilinear sample at continuous coords (edge clamped) */
static void sample_bilinear(const Image *im, double fx, double fy, unsigned char out[4]) {
    int x0 = (int)floor(fx), y0 = (int)floor(fy);
    double tx = fx - x0, ty = fy - y0;
    const unsigned char *p00 = sample_clamp(im, x0, y0);
    const unsigned char *p10 = sample_clamp(im, x0 + 1, y0);
    const unsigned char *p01 = sample_clamp(im, x0, y0 + 1);
    const unsigned char *p11 = sample_clamp(im, x0 + 1, y0 + 1);
    for (int c = 0; c < 4; c++) {
        double top = p00[c] * (1 - tx) + p10[c] * tx;
        double bot = p01[c] * (1 - tx) + p11[c] * tx;
        out[c] = clampb(top * (1 - ty) + bot * ty);
    }
}

static double luma(const unsigned char *p) {
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
}

/* argument accessors: empty token => default (so "crop=100:100::20" works) */
static double argd(char **a, int n, int i, double d) { return (i < n && a[i][0]) ? atof(a[i]) : d; }
static int    argi(char **a, int n, int i, int d)    { return (i < n && a[i][0]) ? atoi(a[i]) : d; }
static const char *args(char **a, int n, int i, const char *d) { return (i < n && a[i][0]) ? a[i] : d; }

/* ------------------------------------------------------------- geometry */

static Image *f_scale(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    int w = argi(a, n, 0, -1), h = argi(a, n, 1, -1);
    const char *flag = args(a, n, 2, "bilinear");
    if (w == -1 && h == -1) { seterr(err, "scale needs at least one of W/H"); return NULL; }
    if (w == -1) w = (int)lround((double)in->w * h / in->h);
    if (h == -1) h = (int)lround((double)in->h * w / in->w);
    if (!img_dims_ok(w, h)) { seterr(err, "scale target invalid or exceeds safety limit"); return NULL; }

    Image *out = img_alloc(w, h);
    if (!out) { seterr(err, "out of memory"); return NULL; }
    int nearest = !strcmp(flag, "nearest");
    for (int y = 0; y < h; y++) {
        double sy = (y + 0.5) * in->h / h - 0.5;
        for (int x = 0; x < w; x++) {
            double sx = (x + 0.5) * in->w / w - 0.5;
            unsigned char *d = img_at(out, x, y);
            if (nearest) {
                const unsigned char *s = sample_clamp(in, (int)lround(sx), (int)lround(sy));
                memcpy(d, s, 4);
            } else {
                sample_bilinear(in, sx, sy, d);
            }
        }
    }
    return out;
}

static Image *f_crop(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    int cw = argi(a, n, 0, in->w), ch = argi(a, n, 1, in->h);
    int x = argi(a, n, 2, (in->w - cw) / 2);   /* default: centre */
    int y = argi(a, n, 3, (in->h - ch) / 2);
    if (!img_dims_ok(cw, ch)) { seterr(err, "crop dimensions invalid or exceed safety limit"); return NULL; }
    Image *out = img_alloc(cw, ch);
    if (!out) { seterr(err, "out of memory"); return NULL; }
    for (int j = 0; j < ch; j++)
        for (int i = 0; i < cw; i++)
            memcpy(img_at(out, i, j), sample_clamp(in, x + i, y + j), 4);
    /* note: out-of-source pixels clamp to the edge rather than erroring */
    return out;
}

static Image *f_pad(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    int w = argi(a, n, 0, in->w), h = argi(a, n, 1, in->h);
    int x = argi(a, n, 2, (w - in->w) / 2), y = argi(a, n, 3, (h - in->h) / 2);
    unsigned char col[4] = {0, 0, 0, 0};
    if (!parse_color(args(a, n, 4, "transparent"), col)) { seterr(err, "pad: bad colour"); return NULL; }
    if (!img_dims_ok(w, h)) { seterr(err, "pad dimensions invalid or exceed safety limit"); return NULL; }
    Image *out = img_alloc(w, h);
    if (!out) { seterr(err, "out of memory"); return NULL; }
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            memcpy(img_at(out, i, j), col, 4);
    for (int j = 0; j < in->h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= h) continue;
        for (int i = 0; i < in->w; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= w) continue;
            memcpy(img_at(out, dx, dy), img_at(in, i, j), 4);
        }
    }
    return out;
}

static Image *f_hflip(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)a; (void)n; (void)app; (void)err;
    for (int y = 0; y < in->h; y++)
        for (int x = 0; x < in->w / 2; x++) {
            unsigned char t[4];
            unsigned char *l = img_at(in, x, y), *r = img_at(in, in->w - 1 - x, y);
            memcpy(t, l, 4); memcpy(l, r, 4); memcpy(r, t, 4);
        }
    return in;
}

static Image *f_vflip(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)a; (void)n; (void)app; (void)err;
    size_t row = (size_t)in->w * 4;
    unsigned char *t = (unsigned char *)malloc(row);
    if (!t) { seterr(err, "out of memory"); return NULL; }
    for (int y = 0; y < in->h / 2; y++) {
        unsigned char *top = img_at(in, 0, y), *bot = img_at(in, 0, in->h - 1 - y);
        memcpy(t, top, row); memcpy(top, bot, row); memcpy(bot, t, row);
    }
    free(t);
    return in;
}

/* transpose=DEG, DEG in {90,180,270}; default 90 (clockwise) */
static Image *f_transpose(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    int deg = ((argi(a, n, 0, 90) % 360) + 360) % 360;
    if (deg == 180) {
        Image *out = img_alloc(in->w, in->h);
        if (!out) { seterr(err, "out of memory"); return NULL; }
        for (int y = 0; y < in->h; y++)
            for (int x = 0; x < in->w; x++)
                memcpy(img_at(out, in->w - 1 - x, in->h - 1 - y), img_at(in, x, y), 4);
        return out;
    }
    if (deg != 90 && deg != 270) { seterr(err, "transpose: use 90, 180 or 270"); return NULL; }
    Image *out = img_alloc(in->h, in->w);
    if (!out) { seterr(err, "out of memory"); return NULL; }
    for (int y = 0; y < in->h; y++)
        for (int x = 0; x < in->w; x++) {
            int nx, ny;
            if (deg == 90) { nx = in->h - 1 - y; ny = x; }
            else           { nx = y;             ny = in->w - 1 - x; }
            memcpy(img_at(out, nx, ny), img_at(in, x, y), 4);
        }
    return out;
}

/* rotate=DEG[:color] - arbitrary angle, canvas expands to fit */
static Image *f_rotate(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    double deg = argd(a, n, 0, 0);
    unsigned char col[4] = {0, 0, 0, 0};
    if (!parse_color(args(a, n, 1, "transparent"), col)) { seterr(err, "rotate: bad colour"); return NULL; }
    double rad = deg * M_PI / 180.0, c = cos(rad), s = sin(rad);
    double cx = in->w / 2.0, cy = in->h / 2.0;
    double corners[4][2] = {{-cx, -cy}, {cx, -cy}, {cx, cy}, {-cx, cy}};
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (int i = 0; i < 4; i++) {
        double rx = c * corners[i][0] - s * corners[i][1];
        double ry = s * corners[i][0] + c * corners[i][1];
        if (rx < minx) minx = rx; if (rx > maxx) maxx = rx;
        if (ry < miny) miny = ry; if (ry > maxy) maxy = ry;
    }
    int nw = (int)ceil(maxx - minx), nh = (int)ceil(maxy - miny);
    if (nw < 1) nw = 1; if (nh < 1) nh = 1;
    if (!img_dims_ok(nw, nh)) { seterr(err, "rotate result exceeds safety limit (input too large to expand)"); return NULL; }
    Image *out = img_alloc(nw, nh);
    if (!out) { seterr(err, "out of memory"); return NULL; }
    double ncx = nw / 2.0, ncy = nh / 2.0;
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            double dx = x + 0.5 - ncx, dy = y + 0.5 - ncy;
            double sx = c * dx + s * dy + cx;   /* inverse rotation */
            double sy = -s * dx + c * dy + cy;
            unsigned char *d = img_at(out, x, y);
            if (sx >= 0 && sx < in->w && sy >= 0 && sy < in->h)
                sample_bilinear(in, sx, sy, d);
            else
                memcpy(d, col, 4);
        }
    return out;
}

/* ------------------------------------------------------------- colour */

/* apply a per-pixel RGB transform via a 3x3 matrix (alpha untouched) */
static Image *matrix3(Image *in, const double m[9]) {
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        double r = p[0], g = p[1], b = p[2];
        p[0] = clampb(m[0] * r + m[1] * g + m[2] * b);
        p[1] = clampb(m[3] * r + m[4] * g + m[5] * b);
        p[2] = clampb(m[6] * r + m[7] * g + m[8] * b);
    }
    return in;
}

static Image *f_grayscale(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)a; (void)n; (void)app; (void)err;
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        unsigned char g = clampb(luma(p));
        p[0] = p[1] = p[2] = g;
    }
    return in;
}

static Image *f_invert(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)a; (void)n; (void)app; (void)err;
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        p[0] = 255 - p[0]; p[1] = 255 - p[1]; p[2] = 255 - p[2];
    }
    return in;
}

static Image *f_sepia(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)a; (void)n; (void)app; (void)err;
    static const double m[9] = {0.393, 0.769, 0.189, 0.349, 0.686, 0.168, 0.272, 0.534, 0.131};
    return matrix3(in, m);
}

static Image *f_brightness(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app; (void)err;
    double b = argd(a, n, 0, 0) * 255.0;   /* -1..1 */
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        for (int c = 0; c < 3; c++) p[c] = clampb(p[c] + b);
    }
    return in;
}

static Image *f_contrast(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app; (void)err;
    double k = argd(a, n, 0, 1.0);
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        for (int c = 0; c < 3; c++) p[c] = clampb((p[c] - 128.0) * k + 128.0);
    }
    return in;
}

static Image *f_saturation(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app; (void)err;
    double s = argd(a, n, 0, 1.0);
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        double g = luma(p);
        for (int c = 0; c < 3; c++) p[c] = clampb(g + s * (p[c] - g));
    }
    return in;
}

static Image *f_gamma(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    double gm = argd(a, n, 0, 1.0);
    if (gm <= 0) { seterr(err, "gamma must be > 0"); return NULL; }
    unsigned char lut[256];
    for (int v = 0; v < 256; v++) lut[v] = clampb(255.0 * pow(v / 255.0, 1.0 / gm));
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        p[0] = lut[p[0]]; p[1] = lut[p[1]]; p[2] = lut[p[2]];
    }
    return in;
}

static Image *f_hue(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app; (void)err;
    double h = argd(a, n, 0, 0) * M_PI / 180.0;
    double U = cos(h), W = sin(h);
    double m[9] = {
        0.299 + 0.701 * U + 0.168 * W, 0.587 - 0.587 * U + 0.330 * W, 0.114 - 0.114 * U - 0.497 * W,
        0.299 - 0.299 * U - 0.328 * W, 0.587 + 0.413 * U + 0.035 * W, 0.114 - 0.114 * U + 0.292 * W,
        0.299 - 0.300 * U + 1.250 * W, 0.587 - 0.588 * U - 1.050 * W, 0.114 + 0.886 * U - 0.203 * W,
    };
    return matrix3(in, m);
}

static Image *f_threshold(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app; (void)err;
    double t = argd(a, n, 0, 128);
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        unsigned char v = luma(p) >= t ? 255 : 0;
        p[0] = p[1] = p[2] = v;
    }
    return in;
}

static Image *f_opacity(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app; (void)err;
    double o = argd(a, n, 0, 1.0);
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        p[3] = clampb(p[3] * o);
    }
    return in;
}

static Image *f_tint(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    unsigned char col[4] = {255, 255, 255, 255};
    if (!parse_color(args(a, n, 0, "white"), col)) { seterr(err, "tint: bad colour"); return NULL; }
    for (int i = 0; i < in->w * in->h; i++) {
        unsigned char *p = in->px + i * 4;
        for (int c = 0; c < 3; c++) p[c] = clampb(p[c] * col[c] / 255.0);
    }
    return in;
}

/* ----------------------------------------------------------- convolution */

/* generic NxN convolution over RGB (alpha preserved from source) */
static Image *convolve(Image *in, const double *k, int kw, int kh, double div, double bias, char **err) {
    if (div == 0) div = 1;
    Image *out = img_alloc(in->w, in->h);
    if (!out) { seterr(err, "out of memory"); return NULL; }
    int ox = kw / 2, oy = kh / 2;
    for (int y = 0; y < in->h; y++)
        for (int x = 0; x < in->w; x++) {
            double acc[3] = {0, 0, 0};
            for (int ky = 0; ky < kh; ky++)
                for (int kx = 0; kx < kw; kx++) {
                    const unsigned char *s = sample_clamp(in, x + kx - ox, y + ky - oy);
                    double kv = k[ky * kw + kx];
                    acc[0] += kv * s[0]; acc[1] += kv * s[1]; acc[2] += kv * s[2];
                }
            unsigned char *d = img_at(out, x, y);
            d[0] = clampb(acc[0] / div + bias);
            d[1] = clampb(acc[1] / div + bias);
            d[2] = clampb(acc[2] / div + bias);
            d[3] = img_at(in, x, y)[3];
        }
    return out;
}

/* separable blur with a 1D kernel of radius r (blurs RGB and alpha) */
static Image *sep_blur(Image *in, const double *ker, int r, char **err) {
    Image *tmp = img_alloc(in->w, in->h);
    Image *out = img_alloc(in->w, in->h);
    if (!tmp || !out) { img_free(tmp); img_free(out); seterr(err, "out of memory"); return NULL; }
    /* horizontal pass: in -> tmp */
    for (int y = 0; y < in->h; y++)
        for (int x = 0; x < in->w; x++) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -r; i <= r; i++) {
                const unsigned char *s = sample_clamp(in, x + i, y);
                double w = ker[i + r];
                for (int c = 0; c < 4; c++) acc[c] += w * s[c];
            }
            unsigned char *d = img_at(tmp, x, y);
            for (int c = 0; c < 4; c++) d[c] = clampb(acc[c]);
        }
    /* vertical pass: tmp -> out */
    for (int y = 0; y < in->h; y++)
        for (int x = 0; x < in->w; x++) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -r; i <= r; i++) {
                const unsigned char *s = sample_clamp(tmp, x, y + i);
                double w = ker[i + r];
                for (int c = 0; c < 4; c++) acc[c] += w * s[c];
            }
            unsigned char *d = img_at(out, x, y);
            for (int c = 0; c < 4; c++) d[c] = clampb(acc[c]);
        }
    img_free(tmp);
    return out;
}

static Image *f_blur(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    int r = argi(a, n, 0, 1);
    if (r < 1) { seterr(err, "blur radius must be >= 1"); return NULL; }
    int len = 2 * r + 1;
    double *ker = (double *)malloc(sizeof(double) * len);
    if (!ker) { seterr(err, "out of memory"); return NULL; }
    for (int i = 0; i < len; i++) ker[i] = 1.0 / len;   /* box */
    Image *out = sep_blur(in, ker, r, err);
    free(ker);
    return out;
}

static Image *f_gblur(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    double sigma = argd(a, n, 0, 1.0);
    if (sigma <= 0) { seterr(err, "gblur sigma must be > 0"); return NULL; }
    int r = (int)ceil(sigma * 3.0);
    if (r < 1) r = 1;
    int len = 2 * r + 1;
    double *ker = (double *)malloc(sizeof(double) * len), sum = 0;
    if (!ker) { seterr(err, "out of memory"); return NULL; }
    for (int i = -r; i <= r; i++) { ker[i + r] = exp(-(double)i * i / (2 * sigma * sigma)); sum += ker[i + r]; }
    for (int i = 0; i < len; i++) ker[i] /= sum;
    Image *out = sep_blur(in, ker, r, err);
    free(ker);
    return out;
}

static Image *f_sharpen(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    double amt = argd(a, n, 0, 1.0);
    double k[9] = {
        0,        -amt,         0,
        -amt, 1 + 4 * amt,   -amt,
        0,        -amt,         0,
    };
    return convolve(in, k, 3, 3, 1, 0, err);
}

static Image *f_emboss(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)a; (void)n; (void)app;
    static const double k[9] = {-2, -1, 0, -1, 1, 1, 0, 1, 2};
    return convolve(in, k, 3, 3, 1, 128, err);
}

/* sobel edge magnitude on luma */
static Image *f_edge(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)a; (void)n; (void)app;
    Image *out = img_alloc(in->w, in->h);
    if (!out) { seterr(err, "out of memory"); return NULL; }
    static const int gx[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    static const int gy[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
    for (int y = 0; y < in->h; y++)
        for (int x = 0; x < in->w; x++) {
            double sx = 0, sy = 0;
            for (int ky = 0; ky < 3; ky++)
                for (int kx = 0; kx < 3; kx++) {
                    double l = luma(sample_clamp(in, x + kx - 1, y + ky - 1));
                    sx += gx[ky * 3 + kx] * l;
                    sy += gy[ky * 3 + kx] * l;
                }
            unsigned char m = clampb(sqrt(sx * sx + sy * sy));
            unsigned char *d = img_at(out, x, y);
            d[0] = d[1] = d[2] = m;
            d[3] = img_at(in, x, y)[3];
        }
    return out;
}

static Image *f_convolution(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    const char *ks = args(a, n, 0, NULL);
    if (!ks) { seterr(err, "convolution needs a kernel, e.g. convolution=0 -1 0 -1 5 -1 0 -1 0"); return NULL; }
    double k[121]; int cnt = 0;          /* up to 11x11 */
    const char *p = ks;
    while (*p && cnt < 121) {
        char *end;
        double v = strtod(p, &end);
        if (end == p) { p++; continue; }
        k[cnt++] = v;
        p = end;
    }
    int side = (int)lround(sqrt((double)cnt));
    if (side * side != cnt || side < 1) { seterr(err, "convolution kernel must be a perfect square (9, 25, ...)"); return NULL; }
    double defdiv = 0; for (int i = 0; i < cnt; i++) defdiv += k[i];
    double div = argd(a, n, 1, defdiv == 0 ? 1 : defdiv);
    double bias = argd(a, n, 2, 0);
    return convolve(in, k, side, side, div, bias, err);
}

/* ----------------------------------------------------- composite / draw */

/* "over" alpha compositing of src pixel (straight alpha) onto dst in place */
static void blend_over(unsigned char *dst, const unsigned char *src) {
    double sa = src[3] / 255.0;
    double da = dst[3] / 255.0;
    double oa = sa + da * (1 - sa);
    for (int c = 0; c < 3; c++) {
        double v = src[c] * sa + dst[c] * da * (1 - sa);
        dst[c] = clampb(oa > 0 ? v / oa : 0);
    }
    dst[3] = clampb(oa * 255.0);
}

static Image *f_overlay(char **a, int n, Image *in, AppContext *app, char **err) {
    int x = argi(a, n, 0, 0), y = argi(a, n, 1, 0);
    int idx = argi(a, n, 2, 1);
    if (idx < 0 || idx >= app->ninputs) { seterr(err, "input index out of range (load it with -i)"); return NULL; }
    Image *ov = app->inputs[idx];
    for (int j = 0; j < ov->h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= in->h) continue;
        for (int i = 0; i < ov->w; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= in->w) continue;
            blend_over(img_at(in, dx, dy), img_at(ov, i, j));
        }
    }
    return in;
}

static Image *f_fill(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    unsigned char col[4] = {0, 0, 0, 255};
    if (!parse_color(args(a, n, 0, "black"), col)) { seterr(err, "fill: bad colour"); return NULL; }
    for (int i = 0; i < in->w * in->h; i++) memcpy(in->px + i * 4, col, 4);
    return in;
}

static Image *f_drawbox(char **a, int n, Image *in, AppContext *app, char **err) {
    (void)app;
    int x = argi(a, n, 0, 0), y = argi(a, n, 1, 0);
    int w = argi(a, n, 2, in->w), h = argi(a, n, 3, in->h);
    unsigned char col[4] = {255, 255, 255, 255};
    if (!parse_color(args(a, n, 4, "white"), col)) { seterr(err, "drawbox: bad colour"); return NULL; }
    int filled = !strcmp(args(a, n, 5, "border"), "fill");
    int t = filled ? 1 : argi(a, n, 5, 1);   /* if not "fill", arg5 may be thickness */
    if (filled) t = 1;
    for (int j = 0; j < h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= in->h) continue;
        for (int i = 0; i < w; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= in->w) continue;
            int on_border = filled ? 1 : (i < t || i >= w - t || j < t || j >= h - t);
            if (on_border) blend_over(img_at(in, dx, dy), col);
        }
    }
    return in;
}

/* ---------------------------------------------------------- registry */

typedef Image *(*FilterFn)(char **a, int n, Image *in, AppContext *app, char **err);

typedef struct { const char *name, *usage; FilterFn fn; } FilterDef;

static const FilterDef FILTERS[] = {
    /* geometry */
    {"scale",       "scale=W:H[:nearest|bilinear]   -1 keeps aspect ratio",      f_scale},
    {"crop",        "crop=W:H[:X:Y]                  default centred",            f_crop},
    {"pad",         "pad=W:H[:X:Y[:color]]           letterbox onto a canvas",    f_pad},
    {"hflip",       "hflip                           mirror horizontally",        f_hflip},
    {"vflip",       "vflip                           mirror vertically",          f_vflip},
    {"transpose",   "transpose=90|180|270            90-degree rotations",        f_transpose},
    {"rotate",      "rotate=DEG[:color]              arbitrary angle, expands",   f_rotate},
    /* colour */
    {"grayscale",   "grayscale                       luma desaturate",            f_grayscale},
    {"gray",        "gray                            alias of grayscale",         f_grayscale},
    {"invert",      "invert                          photographic negative",      f_invert},
    {"negate",      "negate                          alias of invert",            f_invert},
    {"sepia",       "sepia                           warm vintage tone",          f_sepia},
    {"brightness",  "brightness=V                    add V*255, V in -1..1",      f_brightness},
    {"contrast",    "contrast=V                      1=none, >1 punchier",        f_contrast},
    {"saturation",  "saturation=V                    0=gray, 1=none, >1 vivid",   f_saturation},
    {"gamma",       "gamma=V                         >1 brightens midtones",      f_gamma},
    {"hue",         "hue=DEG                          rotate colour wheel",       f_hue},
    {"threshold",   "threshold=V                     binarise on luma (0..255)",  f_threshold},
    {"opacity",     "opacity=V                       scale alpha, V in 0..1",     f_opacity},
    {"tint",        "tint=color                      multiply by a colour",       f_tint},
    /* convolution */
    {"blur",        "blur=R                          box blur, radius R",         f_blur},
    {"gblur",       "gblur=SIGMA                     gaussian blur",              f_gblur},
    {"sharpen",     "sharpen[=AMOUNT]                unsharp 3x3",                f_sharpen},
    {"edge",        "edge                            sobel edge detect",         f_edge},
    {"emboss",      "emboss                          3x3 emboss",                f_emboss},
    {"convolution", "convolution=K[:DIV:BIAS]        custom NxN kernel",         f_convolution},
    /* composite / draw */
    {"overlay",     "overlay=X:Y[:INDEX]             alpha-composite an input",  f_overlay},
    {"fill",        "fill=color                      flood the whole frame",     f_fill},
    {"drawbox",     "drawbox=X:Y:W:H:color[:fill|thickness]  rectangle",         f_drawbox},
};
static const int NFILTERS = (int)(sizeof(FILTERS) / sizeof(FILTERS[0]));

void filters_print_list(void) {
    printf("Filters (chain with commas, e.g. -vf \"scale=800:-1,grayscale,gblur=2\"):\n\n");
    for (int i = 0; i < NFILTERS; i++)
        printf("  %s\n", FILTERS[i].usage);
}

/* trim leading/trailing ascii whitespace in place, returns start */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

Image *run_filtergraph(const char *graph, Image *base, AppContext *app, char **err) {
    Image *cur = img_clone(base);
    if (!cur) { seterr(err, "out of memory"); return NULL; }
    if (!graph || !*graph) return cur;

    char *copy = dupstr(graph);
    if (!copy) { img_free(cur); seterr(err, "out of memory"); return NULL; }

    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *spec = trim(tok);
        if (!*spec) continue;

        char *eq = strchr(spec, '=');
        char name[64] = {0};
        const char *argstr = "";
        if (eq) {
            size_t ln = (size_t)(eq - spec);
            if (ln >= sizeof(name)) ln = sizeof(name) - 1;
            memcpy(name, spec, ln);
            argstr = eq + 1;
        } else {
            strncpy(name, spec, sizeof(name) - 1);
        }

        const FilterDef *def = NULL;
        for (int i = 0; i < NFILTERS; i++)
            if (!strcmp(FILTERS[i].name, name)) { def = &FILTERS[i]; break; }
        if (!def) {
            char m[128];
            snprintf(m, sizeof(m), "unknown filter '%s' (try -filters)", name);
            seterr(err, m);
            free(copy); img_free(cur); return NULL;
        }

        /* split args on ':' */
        char *acopy = dupstr(argstr);
        char *argv[16]; int argc = 0;
        if (acopy && *acopy) {
            char *as = NULL;
            for (char *t = strtok_r(acopy, ":", &as); t && argc < 16; t = strtok_r(NULL, ":", &as))
                argv[argc++] = t;
        }

        char *ferr = NULL;
        Image *out = def->fn(argv, argc, cur, app, &ferr);
        free(acopy);
        if (!out) {
            char m[160];
            snprintf(m, sizeof(m), "%s: %s", name, ferr ? ferr : "failed");
            free(ferr);
            seterr(err, m);
            free(copy); img_free(cur); return NULL;
        }
        if (out != cur) img_free(cur);
        cur = out;
    }
    free(copy);
    return cur;
}

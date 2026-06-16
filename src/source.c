#include "source.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);   /* exact-sized buffer; bounded copy */
    return d;
}
static void seterr(char **err, const char *m) { if (err) *err = dupstr(m); }

static int starts(const char *s, const char *pfx) {
    return strncmp(s, pfx, strlen(pfx)) == 0;
}

int source_is_generator(const char *spec) {
    return starts(spec, "color=") || starts(spec, "gradient=") ||
           starts(spec, "checker=") || starts(spec, "testsrc=");
}

/* split "a:b:c" into argv (in place on a dup); returns count */
static int split_colon(char *s, char **argv, int max) {
    int n = 0;
    char *save = NULL;
    for (char *t = strtok_r(s, ":", &save); t && n < max; t = strtok_r(NULL, ":", &save))
        argv[n++] = t;
    return n;
}

Image *source_generate(const char *spec, char **err) {
    char *eq = strchr(spec, '=');
    if (!eq) { seterr(err, "bad generator spec"); return NULL; }
    char *rest = dupstr(eq + 1);
    char *argv[8];
    int argc = split_colon(rest, argv, 8);

    Image *out = NULL;

    if (starts(spec, "color=")) {
        /* color=COLOR:WxH */
        unsigned char col[4] = {0, 0, 0, 255};
        int w = 320, h = 240;
        if (argc < 1 || !parse_color(argv[0], col)) { seterr(err, "color: need color=NAME:WxH"); goto done; }
        if (argc >= 2 && !parse_size(argv[1], &w, &h)) { seterr(err, "color: bad size"); goto done; }
        if (!img_dims_ok(w, h)) { seterr(err, "color: size exceeds safety limit"); goto done; }
        out = img_alloc(w, h);
        if (out) for (int i = 0; i < w * h; i++) memcpy(out->px + i * 4, col, 4);

    } else if (starts(spec, "gradient=")) {
        /* gradient=WxH[:C1:C2] */
        int w = 320, h = 240;
        unsigned char c1[4] = {0, 0, 0, 255}, c2[4] = {255, 255, 255, 255};
        if (argc < 1 || !parse_size(argv[0], &w, &h)) { seterr(err, "gradient: need gradient=WxH"); goto done; }
        if (argc >= 2 && !parse_color(argv[1], c1)) { seterr(err, "gradient: bad colour 1"); goto done; }
        if (argc >= 3 && !parse_color(argv[2], c2)) { seterr(err, "gradient: bad colour 2"); goto done; }
        if (!img_dims_ok(w, h)) { seterr(err, "gradient: size exceeds safety limit"); goto done; }
        out = img_alloc(w, h);
        if (out)
            for (int x = 0; x < w; x++) {
                double t = w > 1 ? (double)x / (w - 1) : 0;
                unsigned char px[4];
                for (int c = 0; c < 4; c++) px[c] = (unsigned char)(c1[c] * (1 - t) + c2[c] * t + 0.5);
                for (int y = 0; y < h; y++) memcpy(out->px + ((size_t)y * w + x) * 4, px, 4);
            }

    } else if (starts(spec, "checker=")) {
        /* checker=WxH[:SIZE] */
        int w = 320, h = 240, sz = 32;
        if (argc < 1 || !parse_size(argv[0], &w, &h)) { seterr(err, "checker: need checker=WxH"); goto done; }
        if (argc >= 2) sz = atoi(argv[1]); if (sz < 1) sz = 1;
        if (!img_dims_ok(w, h)) { seterr(err, "checker: size exceeds safety limit"); goto done; }
        out = img_alloc(w, h);
        if (out)
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int on = ((x / sz) + (y / sz)) & 1;
                    unsigned char v = on ? 220 : 40;
                    unsigned char *p = out->px + ((size_t)y * w + x) * 4;
                    p[0] = p[1] = p[2] = v; p[3] = 255;
                }

    } else if (starts(spec, "testsrc=")) {
        /* testsrc=WxH : SMPTE-ish bars + ramp + grid */
        int w = 320, h = 240;
        if (argc < 1 || !parse_size(argv[0], &w, &h)) { seterr(err, "testsrc: need testsrc=WxH"); goto done; }
        if (!img_dims_ok(w, h)) { seterr(err, "testsrc: size exceeds safety limit"); goto done; }
        out = img_alloc(w, h);
        if (out) {
            static const unsigned char bars[7][3] = {
                {192, 192, 192}, {192, 192, 0}, {0, 192, 192}, {0, 192, 0},
                {192, 0, 192}, {192, 0, 0}, {0, 0, 192}};
            int barsh = h * 2 / 3;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    unsigned char *p = out->px + ((size_t)y * w + x) * 4;
                    p[3] = 255;
                    if (y < barsh) {
                        const unsigned char *b = bars[(x * 7) / w];
                        p[0] = b[0]; p[1] = b[1]; p[2] = b[2];
                    } else {
                        unsigned char g = (unsigned char)((x * 255) / (w > 1 ? w - 1 : 1));
                        p[0] = p[1] = p[2] = g;
                    }
                    if (x % 32 == 0 || y % 32 == 0) { p[0] = p[1] = p[2] = 80; }  /* grid */
                }
        }
    }

    if (!out && err && !*err) seterr(err, "generator failed (out of memory?)");

done:
    free(rest);
    return out;
}

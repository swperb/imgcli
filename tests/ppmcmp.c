/* ppmcmp A B [tol] - compare two binary P6 PPMs.
 *
 * Exit 0 if they have the same dimensions and every channel differs by <= tol;
 * else exit 1 (and print the mismatch). A small tolerance absorbs cross-platform
 * floating-point ULP differences (libm trig, FMA contraction) while still
 * catching real regressions, which shift pixels by far more than a few levels.
 */
#include <stdio.h>
#include <stdlib.h>

static unsigned char *read_ppm(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[3] = {0};
    int maxv = 0;
    if (fscanf(f, "%2s", magic) != 1 || magic[0] != 'P' || magic[1] != '6' ||
        fscanf(f, "%d %d %d", w, h, &maxv) != 3 || *w <= 0 || *h <= 0) { fclose(f); return NULL; }
    fgetc(f);   /* the single whitespace after maxval */
    size_t n = (size_t)*w * *h * 3;
    unsigned char *d = (unsigned char *)malloc(n);
    if (!d || fread(d, 1, n, f) != n) { free(d); fclose(f); return NULL; }
    fclose(f);
    return d;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: ppmcmp A B [tol]\n"); return 2; }
    int tol = argc > 3 ? atoi(argv[3]) : 0;
    int aw, ah, bw, bh;
    unsigned char *a = read_ppm(argv[1], &aw, &ah);
    unsigned char *b = read_ppm(argv[2], &bw, &bh);
    if (!a || !b) { fprintf(stderr, "ppmcmp: cannot read %s\n", a ? argv[2] : argv[1]); free(a); free(b); return 2; }
    int rc = 0;
    if (aw != bw || ah != bh) {
        fprintf(stderr, "ppmcmp: dimension mismatch %dx%d vs %dx%d\n", aw, ah, bw, bh);
        rc = 1;
    } else {
        size_t n = (size_t)aw * ah * 3;
        int maxd = 0;
        for (size_t i = 0; i < n; i++) { int d = (int)a[i] - (int)b[i]; if (d < 0) d = -d; if (d > maxd) maxd = d; }
        if (maxd > tol) { fprintf(stderr, "ppmcmp: max channel diff %d > tol %d\n", maxd, tol); rc = 1; }
    }
    free(a); free(b);
    return rc;
}

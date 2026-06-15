#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    return -1;
}

/* Read n hex digits into a byte (n is 1 or 2). Returns -1 on bad input. */
static int hex_byte(const char *s, int n) {
    if (n == 1) {
        int v = hexval((unsigned char)s[0]);
        return v < 0 ? -1 : v * 17;           /* #abc -> #aabbcc expansion */
    }
    int hi = hexval((unsigned char)s[0]), lo = hexval((unsigned char)s[1]);
    if (hi < 0 || lo < 0) return -1;
    return hi * 16 + lo;
}

struct named { const char *name; unsigned char rgba[4]; };

static const struct named NAMED[] = {
    {"black",       {0, 0, 0, 255}},
    {"white",       {255, 255, 255, 255}},
    {"red",         {255, 0, 0, 255}},
    {"green",       {0, 128, 0, 255}},
    {"lime",        {0, 255, 0, 255}},
    {"blue",        {0, 0, 255, 255}},
    {"yellow",      {255, 255, 0, 255}},
    {"cyan",        {0, 255, 255, 255}},
    {"magenta",     {255, 0, 255, 255}},
    {"gray",        {128, 128, 128, 255}},
    {"grey",        {128, 128, 128, 255}},
    {"orange",      {255, 165, 0, 255}},
    {"purple",      {128, 0, 128, 255}},
    {"transparent", {0, 0, 0, 0}},
    {"none",        {0, 0, 0, 0}},
};

int parse_color(const char *s, unsigned char out[4]) {
    if (!s || !*s) return 0;

    /* 0x / # hex forms */
    const char *h = NULL;
    if (s[0] == '#') h = s + 1;
    else if ((s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) h = s + 2;

    if (h) {
        size_t len = strlen(h);
        out[3] = 255;
        if (len == 3 || len == 4) {                  /* #rgb / #rgba */
            for (size_t i = 0; i < len; i++) {
                int v = hex_byte(h + i, 1);
                if (v < 0) return 0;
                out[i] = (unsigned char)v;
            }
            return 1;
        }
        if (len == 6 || len == 8) {                  /* #rrggbb / #rrggbbaa */
            for (size_t i = 0; i < len; i += 2) {
                int v = hex_byte(h + i, 2);
                if (v < 0) return 0;
                out[i / 2] = (unsigned char)v;
            }
            return 1;
        }
        return 0;
    }

    /* dash-separated decimals: r-g-b or r-g-b-a */
    if (strchr(s, '-')) {
        int vals[4], n = 0;
        const char *p = s;
        while (*p && n < 4) {
            char *end;
            long v = strtol(p, &end, 10);
            if (end == p) return 0;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            vals[n++] = (int)v;
            p = end;
            if (*p == '-') p++;
            else break;
        }
        if (n < 3) return 0;
        out[0] = (unsigned char)vals[0];
        out[1] = (unsigned char)vals[1];
        out[2] = (unsigned char)vals[2];
        out[3] = (unsigned char)(n == 4 ? vals[3] : 255);
        return 1;
    }

    /* named */
    for (size_t i = 0; i < sizeof(NAMED) / sizeof(NAMED[0]); i++) {
        if (!strcasecmp(s, NAMED[i].name)) {
            memcpy(out, NAMED[i].rgba, 4);
            return 1;
        }
    }
    return 0;
}

int parse_size(const char *s, int *w, int *h) {
    if (!s) return 0;
    char *end;
    long ww = strtol(s, &end, 10);
    if (end == s || (*end != 'x' && *end != 'X')) return 0;
    const char *hs = end + 1;
    long hh = strtol(hs, &end, 10);
    if (end == hs || ww <= 0 || hh <= 0) return 0;
    *w = (int)ww;
    *h = (int)hh;
    return 1;
}

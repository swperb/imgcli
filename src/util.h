/* util.h - small shared parsing helpers. */
#ifndef IMGCLI_UTIL_H
#define IMGCLI_UTIL_H

/* Parse a colour into out[4] = {R,G,B,A}. Accepts (no commas, so it is safe
 * inside a colon-delimited filtergraph):
 *   #rgb  #rrggbb  #rrggbbaa   0xrrggbb[aa]
 *   r-g-b  r-g-b-a              (decimal 0..255, dash separated)
 *   names: black white red green blue yellow cyan magenta gray/grey
 *          orange purple transparent none
 * Returns 1 on success, 0 on failure. */
int parse_color(const char *s, unsigned char out[4]);

/* Parse "WxH" (e.g. "320x240") into *w,*h. Returns 1 on success. */
int parse_size(const char *s, int *w, int *h);

#endif /* IMGCLI_UTIL_H */

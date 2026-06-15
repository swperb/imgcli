/* source.h - synthetic input generators, à la ffmpeg's lavfi sources.
 *
 * A -i argument that isn't a readable file is parsed as a generator spec:
 *   color=COLOR:WxH        solid colour
 *   gradient=WxH[:C1:C2]   horizontal gradient (default black->white)
 *   checker=WxH[:SIZE]     checkerboard
 *   testsrc=WxH            colour bars + greyscale ramp + grid (a test card)
 */
#ifndef IMGCLI_SOURCE_H
#define IMGCLI_SOURCE_H

#include "image.h"

/* Returns 1 if `spec` names a known generator (so the caller shouldn't try to
 * read it as a file). */
int source_is_generator(const char *spec);

/* Build an image from a generator spec. NULL + *err on failure. */
Image *source_generate(const char *spec, char **err);

#endif /* IMGCLI_SOURCE_H */

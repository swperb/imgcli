/* filters.h - the filtergraph engine.
 *
 * Mirrors ffmpeg's -vf model: a comma-separated chain of "name=a:b:c" filters.
 * Filters run left to right; each takes the current frame and the set of all
 * loaded inputs (so compositing filters like overlay can reach a second image).
 */
#ifndef IMGCLI_FILTERS_H
#define IMGCLI_FILTERS_H

#include "image.h"

typedef struct {
    Image **inputs;   /* all -i sources; inputs[0] is the primary frame */
    int     ninputs;
} AppContext;

/* Apply a filtergraph string to `base`. Returns a NEW image; the caller still
 * owns `base` and every AppContext input (they are never freed here).
 * On error returns NULL and sets *err to a malloc'd message. graph may be NULL
 * or "" (identity copy). */
Image *run_filtergraph(const char *graph, Image *base, AppContext *app, char **err);

/* Print the filter catalogue to stdout (for `imgcli -filters`). */
void filters_print_list(void);

/* Print one filter's help by exact name (`imgcli -filters NAME`), honouring
 * --json. Returns 1 if found, 0 if no filter has that name. */
int filters_print_single(const char *name, int json);

/* Print the filter catalogue as a JSON array of {name, syntax, description}
 * to stdout (for `imgcli -filters --json`). */
void filters_print_json(void);

#endif /* IMGCLI_FILTERS_H */

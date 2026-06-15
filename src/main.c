/* main.c - imgcli: an ffmpeg-style command line for still images.
 *
 *   imgcli [-i INPUT]... [-vf GRAPH] [-q N] [-y|-n] OUTPUT
 *
 * INPUT is a file (any format stb decodes) or a synthetic source such as
 * "testsrc=640x480" or "color=red:320x240". The first input is the primary
 * frame; later inputs are reachable from the overlay filter. OUTPUT's format
 * is chosen from its extension (png/jpg/bmp/tga/ppm).
 */
#include "filters.h"
#include "image.h"
#include "source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_INPUTS 16

static void usage(FILE *f) {
    fprintf(f,
        "imgcli - ffmpeg-style image processing in C\n"
        "\n"
        "Usage:\n"
        "  imgcli [-i INPUT]... [-vf GRAPH] [-q N] [-y|-n] OUTPUT\n"
        "\n"
        "Options:\n"
        "  -i INPUT     input file, or a generator (testsrc=WxH, color=NAME:WxH,\n"
        "               gradient=WxH, checker=WxH). Repeat for compositing inputs.\n"
        "  -vf GRAPH    filtergraph, e.g. \"scale=800:-1,grayscale,gblur=2\"\n"
        "  -q N         JPEG quality 1..100 (default 90)\n"
        "  -y           overwrite OUTPUT without asking\n"
        "  -n           never overwrite OUTPUT\n"
        "  -filters     list available filters and exit\n"
        "  -info        print info about each input and exit\n"
        "  -h, --help   show this help\n"
        "\n"
        "Output formats (by extension): png jpg jpeg bmp tga ppm\n"
        "\n"
        "Examples:\n"
        "  imgcli -i photo.jpg -vf \"scale=1024:-1\" thumb.png\n"
        "  imgcli -i photo.jpg -vf \"grayscale,contrast=1.2,gblur=1.5\" out.jpg\n"
        "  imgcli -i bg.png -i logo.png -vf \"overlay=20:20\" out.png\n"
        "  imgcli -i testsrc=640x480 card.png\n");
}

int main(int argc, char **argv) {
    const char *inputs[MAX_INPUTS];
    int ninputs = 0;
    const char *graph = NULL;
    const char *output = NULL;
    int quality = 90;
    int overwrite = -1;        /* -1 ask/refuse, 1 = -y, 0 = -n */
    int want_filters = 0, want_info = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) { usage(stdout); return 0; }
        else if (!strcmp(arg, "-filters")) want_filters = 1;
        else if (!strcmp(arg, "-info")) want_info = 1;
        else if (!strcmp(arg, "-y")) overwrite = 1;
        else if (!strcmp(arg, "-n")) overwrite = 0;
        else if (!strcmp(arg, "-i")) {
            if (i + 1 >= argc) { fprintf(stderr, "imgcli: -i needs an argument\n"); return 2; }
            if (ninputs >= MAX_INPUTS) { fprintf(stderr, "imgcli: too many inputs\n"); return 2; }
            inputs[ninputs++] = argv[++i];
        }
        else if (!strcmp(arg, "-vf")) {
            if (i + 1 >= argc) { fprintf(stderr, "imgcli: -vf needs an argument\n"); return 2; }
            graph = argv[++i];
        }
        else if (!strcmp(arg, "-q")) {
            if (i + 1 >= argc) { fprintf(stderr, "imgcli: -q needs an argument\n"); return 2; }
            quality = atoi(argv[++i]);
        }
        else if (arg[0] == '-' && arg[1]) {
            fprintf(stderr, "imgcli: unknown option '%s' (try -h)\n", arg);
            return 2;
        }
        else {
            if (output) { fprintf(stderr, "imgcli: multiple outputs given ('%s' and '%s')\n", output, arg); return 2; }
            output = arg;
        }
    }

    if (want_filters) { filters_print_list(); return 0; }

    if (ninputs == 0) {
        fprintf(stderr, "imgcli: no input (use -i). Try -h for help.\n");
        return 2;
    }

    /* Load every input (file or generator). */
    Image *loaded[MAX_INPUTS] = {0};
    int rc = 1;
    for (int i = 0; i < ninputs; i++) {
        char *err = NULL;
        Image *im;
        if (source_is_generator(inputs[i]))
            im = source_generate(inputs[i], &err);
        else
            im = img_load(inputs[i], &err);
        if (!im) {
            fprintf(stderr, "imgcli: cannot open input '%s': %s\n", inputs[i], err ? err : "unknown");
            free(err);
            goto cleanup;
        }
        loaded[i] = im;
    }

    if (want_info) {
        for (int i = 0; i < ninputs; i++)
            printf("input %d: %s  %dx%d RGBA\n", i, inputs[i], loaded[i]->w, loaded[i]->h);
        rc = 0;
        goto cleanup;
    }

    if (!output) {
        fprintf(stderr, "imgcli: no output file given. Try -h for help.\n");
        goto cleanup;
    }

    /* Overwrite policy. */
    if (access(output, F_OK) == 0) {
        if (overwrite == 0) { fprintf(stderr, "imgcli: '%s' exists and -n was given\n", output); goto cleanup; }
        if (overwrite == -1) {
            fprintf(stderr, "imgcli: '%s' already exists; pass -y to overwrite\n", output);
            goto cleanup;
        }
    }

    /* Run the filtergraph on the primary input. */
    AppContext app = { loaded, ninputs };
    char *err = NULL;
    Image *result = run_filtergraph(graph, loaded[0], &app, &err);
    if (!result) {
        fprintf(stderr, "imgcli: filtergraph error: %s\n", err ? err : "unknown");
        free(err);
        goto cleanup;
    }

    if (!img_save(output, result, quality, &err)) {
        fprintf(stderr, "imgcli: cannot write '%s': %s\n", output, err ? err : "unknown");
        free(err);
        img_free(result);
        goto cleanup;
    }

    printf("imgcli: wrote %s (%dx%d)\n", output, result->w, result->h);
    img_free(result);
    rc = 0;

cleanup:
    for (int i = 0; i < ninputs; i++) img_free(loaded[i]);
    return rc;
}

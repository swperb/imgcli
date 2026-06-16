/* main.c - imgcli: an ffmpeg-style command line for still images.
 *
 *   imgcli [-i INPUT]... [-vf GRAPH] [-q N] [-y|-n] [--json] [--quiet] OUTPUT
 *
 * INPUT is a file (any format stb decodes) or a synthetic source such as
 * "testsrc=640x480" or "color=red:320x240". The first input is the primary
 * frame; later inputs are reachable from the overlay filter. OUTPUT's format
 * is chosen from its extension (png/jpg/bmp/tga/ppm).
 *
 * Exit codes:  0 = success   1 = runtime error (decode/filter/write)
 *              2 = usage error (bad/missing arguments)
 */
#include "filters.h"
#include "image.h"
#include "source.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     /* strcasecmp */
#include <sys/stat.h>
#include <unistd.h>

#define IMGCLI_VERSION "0.3.0"
#define MAX_INPUTS 16

static void usage(FILE *f) {
    fprintf(f,
        "imgcli " IMGCLI_VERSION " - ffmpeg-style image conversion & processing in C\n"
        "\n"
        "Usage:\n"
        "  imgcli [-i INPUT]... [-vf GRAPH] [-q N] [-f FMT] [-y|-n] [--json] OUTPUT\n"
        "\n"
        "Options:\n"
        "  -i INPUT     input file, '-' for stdin, or a generator (testsrc=WxH,\n"
        "               color=NAME:WxH, gradient=WxH, checker=WxH). Repeat for compositing.\n"
        "  -vf GRAPH    filtergraph, e.g. \"scale=800:-1,grayscale,gblur=2\"\n"
        "  -q N         JPEG quality 1..100 (default 90)\n"
        "  -f FMT       output format (png/jpg/bmp/tga/ppm/qoi); required when OUTPUT is\n"
        "               '-' (stdout), optional override for files\n"
        "  -y           overwrite OUTPUT without asking\n"
        "  -n           never overwrite OUTPUT\n"
        "  --json       emit a single machine-readable JSON result line\n"
        "  --quiet      suppress the human-readable success line\n"
        "  --dry-run    validate the filtergraph and report output dims; write nothing\n"
        "  -filters     list available filters and exit\n"
        "  -info        print info about each input and exit\n"
        "  -V, --version  print version and exit\n"
        "  -h, --help   show this help\n"
        "\n"
        "Output formats: png jpg jpeg bmp tga ppm qoi (by extension, or -f)\n"
        "\n"
        "Examples:\n"
        "  imgcli -i photo.jpg -vf \"scale=1024:-1\" thumb.png\n"
        "  imgcli -i photo.jpg -vf \"grayscale,contrast=1.2,gblur=1.5\" out.jpg\n"
        "  imgcli -i bg.png -i logo.png -vf \"overlay=20:20\" out.png\n"
        "  imgcli --json -y -i in.png -vf \"scale=256:-1\" out.png\n"
        "  curl -s URL | imgcli -i - -vf \"scale=800:-1\" -f jpg - > out.jpg\n");
}

/* Lowercased output format name derived from the file extension. */
static const char *out_format(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "unknown";
    dot++;
    if (!strcasecmp(dot, "png")) return "png";
    if (!strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg")) return "jpeg";
    if (!strcasecmp(dot, "bmp")) return "bmp";
    if (!strcasecmp(dot, "tga")) return "tga";
    if (!strcasecmp(dot, "ppm")) return "ppm";
    if (!strcasecmp(dot, "qoi")) return "qoi";
    return "unknown";
}

/* Emit an error either as a JSON object or a human line; always to stderr. */
static void emit_error(int json, const char *msg) {
    if (json) {
        fputs("{\"ok\":false,\"error\":", stderr);
        json_str(stderr, msg);
        fputs("}\n", stderr);
    } else {
        fprintf(stderr, "imgcli: %s\n", msg);
    }
}

int main(int argc, char **argv) {
    const char *inputs[MAX_INPUTS];
    int ninputs = 0;
    const char *graph = NULL;
    const char *output = NULL;
    const char *format = NULL;     /* -f: output format override (required for stdout) */
    int quality = 90;
    int overwrite = -1;        /* -1 ask/refuse, 1 = -y, 0 = -n */
    int want_filters = 0, want_info = 0, json = 0, quiet = 0, dry_run = 0;
    char msg[512];

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) { usage(stdout); return 0; }
        else if (!strcmp(arg, "-V") || !strcmp(arg, "--version")) { printf("imgcli %s\n", IMGCLI_VERSION); return 0; }
        else if (!strcmp(arg, "-filters")) want_filters = 1;
        else if (!strcmp(arg, "-info")) want_info = 1;
        else if (!strcmp(arg, "--json")) json = 1;
        else if (!strcmp(arg, "--quiet")) quiet = 1;
        else if (!strcmp(arg, "--dry-run")) dry_run = 1;
        else if (!strcmp(arg, "-y")) overwrite = 1;
        else if (!strcmp(arg, "-n")) overwrite = 0;
        else if (!strcmp(arg, "-i")) {
            if (i + 1 >= argc) { emit_error(json, "-i needs an argument"); return 2; }
            if (ninputs >= MAX_INPUTS) { emit_error(json, "too many inputs"); return 2; }
            inputs[ninputs++] = argv[++i];
        }
        else if (!strcmp(arg, "-vf")) {
            if (i + 1 >= argc) { emit_error(json, "-vf needs an argument"); return 2; }
            graph = argv[++i];
        }
        else if (!strcmp(arg, "-q")) {
            if (i + 1 >= argc) { emit_error(json, "-q needs an argument"); return 2; }
            quality = atoi(argv[++i]);
        }
        else if (!strcmp(arg, "-f")) {
            if (i + 1 >= argc) { emit_error(json, "-f needs an argument"); return 2; }
            format = argv[++i];
        }
        else if (arg[0] == '-' && arg[1]) {
            snprintf(msg, sizeof msg, "unknown option '%s' (try -h)", arg);
            emit_error(json, msg);
            return 2;
        }
        else {
            if (output) {
                snprintf(msg, sizeof msg, "multiple outputs given ('%s' and '%s')", output, arg);
                emit_error(json, msg);
                return 2;
            }
            output = arg;
        }
    }

    if (want_filters) { if (json) filters_print_json(); else filters_print_list(); return 0; }

    if (ninputs == 0) { emit_error(json, "no input (use -i); try -h for help"); return 2; }

    /* Load every input (file or generator). */
    Image *loaded[MAX_INPUTS] = {0};
    int rc = 1;
    for (int i = 0; i < ninputs; i++) {
        char *err = NULL;
        Image *im = !strcmp(inputs[i], "-")           ? img_load_stdin(&err)
                  : source_is_generator(inputs[i])    ? source_generate(inputs[i], &err)
                                                      : img_load(inputs[i], &err);
        if (!im) {
            snprintf(msg, sizeof msg, "cannot open input '%s': %s", inputs[i], err ? err : "unknown");
            emit_error(json, msg);
            free(err);
            goto cleanup;
        }
        loaded[i] = im;
    }

    if (want_info) {
        if (json) {
            fputs("{\"ok\":true,\"inputs\":[", stdout);
            for (int i = 0; i < ninputs; i++) {
                if (i) fputc(',', stdout);
                fputs("{\"path\":", stdout);
                json_str(stdout, inputs[i]);
                printf(",\"width\":%d,\"height\":%d,\"channels\":4}", loaded[i]->w, loaded[i]->h);
            }
            fputs("]}\n", stdout);
        } else {
            for (int i = 0; i < ninputs; i++)
                printf("input %d: %s  %dx%d RGBA\n", i, inputs[i], loaded[i]->w, loaded[i]->h);
        }
        rc = 0;
        goto cleanup;
    }

    if (!output && !dry_run) { emit_error(json, "no output file given; try -h for help"); goto cleanup; }

    int to_stdout = output && !strcmp(output, "-");
    if (to_stdout && !dry_run && !format) {
        emit_error(json, "writing to stdout (-) requires -f FORMAT (png/jpg/bmp/tga/ppm/qoi)");
        goto cleanup;
    }

    /* Overwrite policy (never prompts; skipped in --dry-run and for stdout). */
    if (!dry_run && !to_stdout && access(output, F_OK) == 0) {
        if (overwrite == 0) {
            snprintf(msg, sizeof msg, "'%s' exists and -n was given", output);
            emit_error(json, msg); goto cleanup;
        }
        if (overwrite == -1) {
            snprintf(msg, sizeof msg, "'%s' already exists; pass -y to overwrite", output);
            emit_error(json, msg); goto cleanup;
        }
    }

    /* Run the filtergraph on the primary input. */
    AppContext app = { loaded, ninputs };
    char *err = NULL;
    Image *result = run_filtergraph(graph, loaded[0], &app, &err);
    if (!result) {
        snprintf(msg, sizeof msg, "filtergraph error: %s", err ? err : "unknown");
        emit_error(json, msg);
        free(err);
        goto cleanup;
    }

    /* --dry-run: the graph parsed and ran; report the output dimensions and stop
     * before encoding/writing anything. */
    if (dry_run) {
        const char *fmt = format ? format : (output ? out_format(output) : "n/a");
        if (json) {
            printf("{\"ok\":true,\"dry_run\":true,\"width\":%d,\"height\":%d,\"format\":\"%s\"}\n",
                   result->w, result->h, fmt);
        } else if (!quiet) {
            printf("imgcli: dry-run OK — %dx%d", result->w, result->h);
            if (output) printf(" (%s)", fmt);
            printf("\n");
        }
        img_free(result);
        rc = 0;
        goto cleanup;
    }

    long long bytes = -1;
    if (!img_write(output, result, format, quality, &bytes, &err)) {
        snprintf(msg, sizeof msg, "cannot write '%s': %s", output, err ? err : "unknown");
        emit_error(json, msg);
        free(err);
        img_free(result);
        goto cleanup;
    }

    /* Success. When piping to stdout the image bytes own stdout, so the result
     * line goes to stderr to keep the pipe clean. */
    {
        const char *fmt = format ? format : out_format(output);
        FILE *rf = to_stdout ? stderr : stdout;
        if (json) {
            fputs("{\"ok\":true,\"output\":", rf);
            json_str(rf, output);
            fprintf(rf, ",\"width\":%d,\"height\":%d,\"format\":\"%s\",\"bytes\":%lld}\n",
                    result->w, result->h, fmt, bytes);
        } else if (!quiet) {
            if (to_stdout)
                fprintf(stderr, "imgcli: wrote %lld bytes to stdout (%dx%d %s)\n",
                        bytes, result->w, result->h, fmt);
            else
                printf("imgcli: wrote %s (%dx%d)\n", output, result->w, result->h);
        }
    }
    img_free(result);
    rc = 0;

cleanup:
    for (int i = 0; i < ninputs; i++) img_free(loaded[i]);
    return rc;
}

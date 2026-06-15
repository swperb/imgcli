/* fuzz_decode.c - libFuzzer harness for imgcli's real attack surface.
 *
 * imgcli's threat model (see SECURITY.md) is: untrusted IMAGE FILES decoded by
 * stb_image, then run through the filter pipeline, then encoded. This harness
 * feeds arbitrary bytes straight into that path:
 *
 *     raw bytes -> img_load_mem (stb decode) -> run_filtergraph -> free
 *
 * Build & run:
 *     make fuzz
 *     ./imgcli-fuzz -max_len=65536            # or point it at a corpus dir
 *
 * It exercises decode (the stb GIF/TGA/PNM/JPEG/PNG paths that have historically
 * had CVEs) plus a representative geometry/colour/convolution/rotate chain, all
 * under ASan+UBSan. Any memory-safety or UB bug aborts with a reproducer.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../src/filters.h"
#include "../src/image.h"

/* A fixed graph that touches every filter family without changing dimensions
 * unboundedly (caps in the code keep allocations sane on adversarial inputs). */
static const char *GRAPH =
    "scale=48:-1,crop=32:32,grayscale,contrast=1.2,gblur=1.5,"
    "sharpen,edge,convolution=0 -1 0 -1 5 -1 0 -1 0,rotate=17,overlay=4:4";

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > (size_t)INT32_MAX) return 0;

    char *err = NULL;
    Image *im = img_load_mem(data, (int)size, &err);
    free(err);
    if (!im) return 0;            /* undecodable input: nothing more to do */

    /* overlay references input index 1; reuse the same frame as a second input. */
    Image *ins[2] = { im, im };
    AppContext app = { ins, 2 };

    char *gerr = NULL;
    Image *out = run_filtergraph(GRAPH, im, &app, &gerr);
    free(gerr);
    if (out) img_free(out);

    img_free(im);
    return 0;
}

#ifdef IMGCLI_FUZZ_STANDALONE
/* Replay driver for platforms without the libFuzzer runtime (e.g. macOS
 * Command Line Tools). Build with `make fuzz-replay` and pass files/corpora;
 * each is fed to the harness once, under ASan/UBSan. Not a fuzzer (no mutation)
 * but validates the harness and the decode path against saved/adversarial bytes.
 */
#include <stdio.h>

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0) {
            uint8_t *buf = (uint8_t *)malloc((size_t)n);
            if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n)
                LLVMFuzzerTestOneInput(buf, (size_t)n);
            free(buf);
        }
        fclose(f);
    }
    return 0;
}
#endif

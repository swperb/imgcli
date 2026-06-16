# imgcli - ffmpeg-style image conversion & processing in C
CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -Wall -Wextra -Wformat -Wformat-security
LDFLAGS ?=
LDLIBS  := -lm

# --- Exploit-mitigation hardening (defense-in-depth; see SECURITY.md) --------
# Rationale: NSA/CISA guidance flags C/C++ as memory-unsafe. Since imgcli stays
# in C for portability, it compensates with compiler/linker hardening, ASan/UBSan
# in CI, and fuzzing of the decode path. Linux-only flags are gated by uname.
UNAME_S := $(shell uname -s)
HARDEN_CFLAGS  := -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fno-strict-aliasing -fPIE
HARDEN_LDFLAGS :=
ifeq ($(UNAME_S),Linux)
  # macOS produces PIE by default, so -pie is Linux-only (avoids an unused-arg warning).
  HARDEN_CFLAGS  += -fstack-clash-protection
  HARDEN_LDFLAGS += -pie -Wl,-z,relro,-z,now,-z,noexecstack
endif
CFLAGS  += $(HARDEN_CFLAGS)
LDFLAGS += $(HARDEN_LDFLAGS)

SRC := src/main.c src/image.c src/filters.c src/source.c src/util.c
OBJ := $(SRC:.c=.o)
BIN := imgcli

PREFIX ?= /usr/local

.PHONY: all clean install demo asan fuzz fuzz-replay check test test-update
all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

# stb headers are large; don't let their warnings clutter the build.
src/image.o: src/image.c
	$(CC) $(CFLAGS) -Wno-unused-but-set-variable -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN) imgcli.asan imgcli-fuzz tests/ppmcmp

# Integration suite: golden pixel output + format round-trips + exit codes +
# --json schema. See tests/run.sh. `make test-update` regenerates the goldens.
tests/ppmcmp: tests/ppmcmp.c
	$(CC) $(CFLAGS) -o $@ $<

test: $(BIN) tests/ppmcmp
	@BIN=./$(BIN) CMP=./tests/ppmcmp bash tests/run.sh

test-update: $(BIN) tests/ppmcmp
	@UPDATE=1 BIN=./$(BIN) CMP=./tests/ppmcmp bash tests/run.sh

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

# AddressSanitizer + UBSan build (catches OOB/UAF/integer-UB at runtime).
asan: $(SRC)
	$(CC) -std=gnu11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	  -Wno-unused-but-set-variable $(SRC) $(LDLIBS) -o imgcli.asan

# libFuzzer harness over decode -> filtergraph. Needs clang WITH the libFuzzer
# runtime (Linux clang, or full Xcode on macOS - the CLT-only clang lacks it).
fuzz: fuzz/fuzz_decode.c $(filter-out src/main.c,$(SRC))
	clang -std=gnu11 -g -O1 -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
	  -Wno-unused-but-set-variable -Wno-deprecated-declarations \
	  fuzz/fuzz_decode.c src/image.c src/filters.c src/source.c src/util.c $(LDLIBS) \
	  -o imgcli-fuzz

# Standalone replay (no libFuzzer runtime needed): feed it files/corpora to
# validate the harness + decode path under ASan/UBSan anywhere.
fuzz-replay: fuzz/fuzz_decode.c $(filter-out src/main.c,$(SRC))
	$(CC) -std=gnu11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	  -DIMGCLI_FUZZ_STANDALONE -Wno-unused-but-set-variable -Wno-deprecated-declarations \
	  fuzz/fuzz_decode.c src/image.c src/filters.c src/source.c src/util.c $(LDLIBS) \
	  -o imgcli-fuzz

# Quick smoke test of the pipeline end to end.
check: $(BIN)
	./$(BIN) -y -i testsrc=64x64 /tmp/imgcli_check.png
	./$(BIN) --json -y -i /tmp/imgcli_check.png -vf "scale=32:-1,grayscale,edge" /tmp/imgcli_check2.png
	./$(BIN) --json -y -i /tmp/imgcli_check.png /tmp/imgcli_check.qoi
	./$(BIN) --json -y -i /tmp/imgcli_check.qoi /tmp/imgcli_check_qoi.png
	./$(BIN) --json --dry-run -i testsrc=64x64 -vf "scale=32:-1,rotate=15"
	./$(BIN) --json -y -i testsrc=120x90 -vf "scale=480:360:bicubic" /tmp/imgcli_bicubic.png
	./$(BIN) --json -y -i testsrc=400x400 -vf "scale=64:64:lanczos" /tmp/imgcli_lanczos.png
	./$(BIN) -i testsrc=64x64 -f png - | ./$(BIN) -i - -vf "scale=32:-1,grayscale" -f qoi - > /tmp/imgcli_pipe.qoi
	@test -s /tmp/imgcli_pipe.qoi && ./$(BIN) --json -info -i /tmp/imgcli_pipe.qoi >/dev/null && echo "  stdin/stdout pipe: OK"
	@rm -rf /tmp/imgcli_bin /tmp/imgcli_out && mkdir -p /tmp/imgcli_bin
	@./$(BIN) -y -i testsrc=40x40 /tmp/imgcli_bin/a.png >/dev/null && ./$(BIN) -y -i gradient=30x30 /tmp/imgcli_bin/b.png >/dev/null
	./$(BIN) --json -i "/tmp/imgcli_bin/*.png" --out-dir /tmp/imgcli_out -vf "scale=20:-1:lanczos" -f qoi -y >/dev/null
	@test -f /tmp/imgcli_out/a.qoi -a -f /tmp/imgcli_out/b.qoi && echo "  batch (--out-dir): OK"
	@command -v python3 >/dev/null && ./$(BIN) -filters --json | python3 -m json.tool >/dev/null && echo "  -filters --json: valid" || echo "  (skipped -filters --json validation: no python3)"
	@echo "check: OK"

# Generate a few sample outputs to eyeball the pipeline.
demo: $(BIN)
	./$(BIN) -y -i testsrc=640x480 demo_card.png
	./$(BIN) -y -i demo_card.png -vf "scale=320:-1,grayscale,edge" demo_edge.png
	./$(BIN) -y -i demo_card.png -vf "gblur=3,contrast=1.3,sepia" demo_stylise.png
	@echo "wrote demo_card.png demo_edge.png demo_stylise.png"

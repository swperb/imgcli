#!/usr/bin/env bash
# Integration test suite for imgcli. Run via `make test`.
#
#   - golden pixel tests: each filter family vs a committed reference PPM,
#     compared with a small tolerance (cross-platform float robustness)
#   - format round-trips (QOI is lossless -> exact)
#   - error / exit-code paths (0 ok, 1 runtime, 2 usage)
#   - --json result + error schema (errors must stay JSON under --json)
#
# UPDATE=1 make test   regenerates the golden PPMs instead of comparing.
set -u
BIN=${BIN:-./imgcli}
CMP=${CMP:-./tests/ppmcmp}
GOLDEN=tests/golden
TOL=${TOL:-3}
UPDATE=${UPDATE:-0}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
pass=0; fail=0
have_py=0; command -v python3 >/dev/null 2>&1 && have_py=1

ok()   { pass=$((pass+1)); }
bad()  { fail=$((fail+1)); echo "  FAIL: $1"; }

# golden NAME ARGS...   (ARGS produce $OUT/NAME.ppm; compared to the golden)
golden() {
    local name="$1"; shift
    if ! "$BIN" -y "$@" "$OUT/$name.ppm" >/dev/null 2>&1; then bad "$name (imgcli errored)"; return; fi
    if [ "$UPDATE" = "1" ]; then cp "$OUT/$name.ppm" "$GOLDEN/$name.ppm"; echo "  updated $name"; return; fi
    if "$CMP" "$GOLDEN/$name.ppm" "$OUT/$name.ppm" "$TOL" >/dev/null 2>&1; then ok
    else bad "$name ($("$CMP" "$GOLDEN/$name.ppm" "$OUT/$name.ppm" "$TOL" 2>&1))"; fi
}

# expect_exit CODE  DESC  ARGS...
expect_exit() {
    local want="$1" desc="$2"; shift 2
    "$BIN" "$@" >/dev/null 2>&1; local got=$?
    if [ "$got" = "$want" ]; then ok; else bad "$desc (exit $got, wanted $want)"; fi
}

mkdir -p "$GOLDEN"

echo "== golden pixel tests (tol=$TOL) =="
golden scale_bilinear  -i testsrc=96x96   -vf "scale=64:48"
golden scale_bicubic   -i testsrc=96x96   -vf "scale=72:54:bicubic"
golden scale_nearest   -i testsrc=48x48   -vf "scale=96:96:nearest"
golden crop            -i testsrc=100x100 -vf "crop=48:48:12:12"
golden pad             -i color=red:32x32 -vf "pad=64:64:16:16:blue"
golden flip            -i gradient=64x64  -vf "hflip,vflip"
golden transpose       -i testsrc=80x40   -vf "transpose=90"
golden grayscale       -i testsrc=64x64   -vf "grayscale"
golden invert          -i testsrc=64x64   -vf "invert"
golden sepia           -i testsrc=64x64   -vf "sepia"
golden brightness      -i color=100-120-140:48x48 -vf "brightness=0.25"
golden contrast        -i testsrc=64x64   -vf "contrast=1.4"
golden saturation      -i testsrc=64x64   -vf "saturation=1.6"
golden threshold       -i testsrc=64x64   -vf "threshold=128"
golden tint            -i gradient=48x48  -vf "tint=#40c0ff"
golden blur            -i testsrc=64x64   -vf "blur=2"
golden sharpen         -i testsrc=64x64   -vf "sharpen"
golden edge            -i testsrc=64x64   -vf "edge"
golden emboss          -i testsrc=64x64   -vf "emboss"
golden convolution     -i testsrc=64x64   -vf "convolution=0 -1 0 -1 5 -1 0 -1 0"
golden overlay         -i color=blue:64x64 -i color=#ff000080:24x24 -vf "overlay=12:12"
golden drawbox         -i color=white:64x64 -vf "drawbox=8:8:40:40:red:3"
golden chain           -i testsrc=128x96  -vf "scale=64:-1,grayscale,contrast=1.2,sharpen,invert"

if [ "$UPDATE" = "1" ]; then echo "goldens updated."; exit 0; fi

echo "== format round-trips =="
"$BIN" -y -i testsrc=70x50 "$OUT/o.ppm" >/dev/null 2>&1
"$BIN" -y -i "$OUT/o.ppm" "$OUT/o.qoi" >/dev/null 2>&1
"$BIN" -y -i "$OUT/o.qoi" "$OUT/o.rt.ppm" >/dev/null 2>&1
if "$CMP" "$OUT/o.ppm" "$OUT/o.rt.ppm" 0 >/dev/null 2>&1; then ok; else bad "qoi round-trip not lossless"; fi
for ext in png bmp tga; do
    "$BIN" -y -i "$OUT/o.ppm" "$OUT/o.$ext" >/dev/null 2>&1 && \
    "$BIN" --json -info -i "$OUT/o.$ext" 2>/dev/null | grep -q '"width":70' && ok || bad "round-trip via $ext"
done

echo "== error / exit-code paths =="
expect_exit 2 "no input"            out.png
expect_exit 2 "unknown option"      --bogus
expect_exit 2 "-i without arg"      -i
expect_exit 1 "missing input file"  -i "$OUT/nope.png" "$OUT/x.png"
expect_exit 1 "bad filter name"     -i testsrc=8x8 -vf "nosuchfilter" "$OUT/x.png"
expect_exit 1 "bad output format"   -i testsrc=8x8 "$OUT/x.weirdext"

echo "== --json schema =="
if [ "$have_py" = 1 ]; then
    # success result
    "$BIN" --json -y -i testsrc=64x64 -vf "scale=32:-1" "$OUT/j.png" 2>/dev/null | python3 -c '
import sys,json; d=json.load(sys.stdin)
assert d["ok"] and d["output"] and d["width"]==32 and d["height"] and d["format"]=="png" and "bytes" in d' && ok || bad "success json schema"
    # ERRORS STAY JSON under --json (the regression guard)
    "$BIN" --json -i "$OUT/nope.png" "$OUT/x.png" 2>&1 1>/dev/null | python3 -c '
import sys,json; d=json.load(sys.stdin)
assert d["ok"] is False and isinstance(d.get("error"), str)' && ok || bad "error stays JSON under --json"
    # -info json
    "$BIN" --json -info -i testsrc=40x30 2>/dev/null | python3 -c '
import sys,json; d=json.load(sys.stdin)
assert d["ok"] and d["inputs"][0]["width"]==40 and d["inputs"][0]["height"]==30' && ok || bad "info json schema"
    # dry-run json
    "$BIN" --json --dry-run -i testsrc=64x64 -vf "scale=20:-1" 2>/dev/null | python3 -c '
import sys,json; d=json.load(sys.stdin)
assert d["ok"] and d["dry_run"] and d["width"]==20' && ok || bad "dry-run json schema"
    # batch json array
    rm -rf "$OUT/bin" "$OUT/bout"; mkdir -p "$OUT/bin"
    "$BIN" -y -i testsrc=40x40 "$OUT/bin/a.png" >/dev/null 2>&1; "$BIN" -y -i gradient=30x30 "$OUT/bin/b.png" >/dev/null 2>&1
    "$BIN" --json -i "$OUT/bin/*.png" --out-dir "$OUT/bout" -vf "scale=16:-1" -f qoi -y 2>/dev/null | python3 -c '
import sys,json; d=json.load(sys.stdin)
assert d["batch"] and d["processed"]==2 and d["failed"]==0 and len(d["results"])==2 and d["results"][0]["ok"]' && ok || bad "batch json schema"
else
    echo "  (skipped --json schema checks: no python3)"
fi

echo
echo "tests: $pass passed, $fail failed"
[ "$fail" = "0" ]

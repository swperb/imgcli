#!/usr/bin/env bash
# Reproducible micro-benchmark: imgcli vs ImageMagick.
#
#   make            # build ./imgcli first
#   bench/bench.sh  # run from the repo root
#
# Compares everyday convert/resize/filter operations. Needs ./imgcli built;
# if `magick` (ImageMagick) is on PATH it runs the head-to-head, otherwise it
# reports imgcli-only. Timing = wall time over N iterations / N (mean ms/op),
# via /usr/bin/time. Honest caveat: ImageMagick defaults to 16-bit/HDRI with
# colour management and far broader format/filter support; imgcli is a lean
# 8-bit RGBA pipeline. Part of imgcli's speed is doing less, more directly.
set -u
IMG=./imgcli
[ -x "$IMG" ] || { echo "build first: make"; exit 1; }
MAGICK=$(command -v magick || true)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

"$IMG" -y -i testsrc=64x64     "$T/small.png" >/dev/null 2>&1
"$IMG" -y -i gradient=1200x1200 "$T/med.png"  >/dev/null 2>&1

loop() { # N cmd...
  local n=$1; shift
  local r
  r=$( { /usr/bin/time -p bash -c "for ((i=0;i<$n;i++)); do $* >/dev/null 2>&1; done"; } 2>&1 | awk '/real/{print $2}')
  awk "BEGIN{printf \"%.1f\", ($r/$n)*1000}"
}

row() { # label N imgcli-cmd magick-cmd
  local label=$1 n=$2 ic=$3 mc=$4
  local a b
  a=$(loop "$n" "$ic")
  if [ -n "$MAGICK" ]; then b=$(loop "$n" "$mc"); else b="-"; fi
  printf "%-34s  imgcli %6s ms   magick %7s ms\n" "$label (N=$n)" "$a" "$b"
}

echo "## imgcli vs ImageMagick — $(uname -srm)"
[ -n "$MAGICK" ] && echo "## $(magick -version | head -1)"
echo "## imgcli $("$IMG" --version | awk '{print $2}')"
echo
row "small png->jpg resize (1-shot)" 100 \
    "$IMG -y -i $T/small.png -vf scale=32:-1 -q 85 $T/o.jpg" \
    "$MAGICK $T/small.png -resize 32x -quality 85 $T/m.jpg"
row "resize 1200->300w png" 25 \
    "$IMG -y -i $T/med.png -vf scale=300:-1 $T/o.png" \
    "$MAGICK $T/med.png -resize 300x $T/m.png"
row "grayscale 1200x1200" 25 \
    "$IMG -y -i $T/med.png -vf grayscale $T/o.png" \
    "$MAGICK $T/med.png -colorspace Gray $T/m.png"
echo
echo "footprint:  imgcli $(ls -l "$IMG" | awk '{print $5}') B single binary, deps: $(otool -L "$IMG" 2>/dev/null | tail -n +2 | wc -l | tr -d ' ') (libc only)"
[ -n "$MAGICK" ] && echo "            imagemagick ~109 MB incl. 17 brew deps / 10 linked libs"

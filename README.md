# imgcli — command-line image conversion & processing in C

**A dependency-free CLI to convert, resize, crop, filter, and composite images** —
PNG, JPEG, BMP, TGA, GIF, and PPM — driven by an ffmpeg-style filtergraph. One
small C binary, no system libraries, compiles anywhere a C11 compiler exists.

> If you're looking for a lightweight **image converter / image processing
> library** for the command line — a tiny alternative to ImageMagick `convert`
> or `ffmpeg` for still images — this is it.

```sh
# Convert + resize + filter in one pass:
imgcli -i photo.jpg -vf "scale=1024:-1,grayscale,contrast=1.2,gblur=1.5" out.png
```

**What it does:** format conversion · resize / scale · crop · pad · rotate · flip ·
brightness / contrast / saturation / gamma / hue · grayscale · sepia · invert ·
threshold · box & Gaussian blur · sharpen · Sobel edge detect · emboss · custom
convolution kernels · alpha-composite overlay · draw boxes · solid fills.

**Design:** the same paradigm as ffmpeg — *decode → normalize to one common frame
format (RGBA) → run a comma-separated filtergraph → encode by output extension.*

## Why it's structured this way

| ffmpeg concept        | imgcli equivalent                                            |
| --------------------- | -------------------------------------------------------------- |
| `AVFrame` / pixfmt    | every image is normalized to packed 8-bit **RGBA** (`Image`)   |
| demuxer/decoder       | `img_load` via vendored **stb_image** (PNG/JPEG/BMP/TGA/GIF/…)  |
| `-vf` filtergraph     | `name=a:b:c, name, …` chain parsed in `filters.c`              |
| `AVFilter`            | each filter mutates or replaces the current frame              |
| muxer (by extension)  | `img_save` (png/jpg/bmp/tga + a hand-written PPM writer)        |
| `lavfi` test sources  | `testsrc=`, `color=`, `gradient=`, `checker=` generators       |

Codecs come from the public-domain [stb](https://github.com/nothings/stb)
single-header libraries (`third_party/`). They're bundled, not linked, so the
tool stays portable and self-contained while still reading/writing the formats
the world actually uses — the same trade-off ffmpeg makes by leaning on codec
libraries instead of reinventing them.

## Build

```sh
make            # produces ./imgcli
make demo       # generates a few sample images
sudo make install   # installs to /usr/local/bin
```

Only the C standard library and `-lm` are required.

## Usage

```
imgcli [-i INPUT]... [-vf GRAPH] [-q N] [-y|-n] OUTPUT

  -i INPUT     a file, or a generator (testsrc=WxH, color=NAME:WxH,
               gradient=WxH, checker=WxH). Repeat -i for compositing inputs;
               the first is the primary frame, the rest feed `overlay`.
  -vf GRAPH    filtergraph, e.g. "scale=800:-1,grayscale,gblur=2"
  -q N         JPEG quality 1..100 (default 90)
  -y / -n      overwrite / never overwrite the output
  -filters     list every filter
  -info        print input dimensions and exit
  -h           help
```

Colours accept `#rgb`, `#rrggbb`, `#rrggbbaa`, `0x…`, `r-g-b[-a]`, or names
(`red`, `white`, `transparent`, …). No commas, so they're safe inside a graph.

## Filters

**Geometry** — `scale=W:H[:nearest|bilinear]` (`-1` keeps aspect),
`crop=W:H[:X:Y]`, `pad=W:H[:X:Y[:color]]`, `hflip`, `vflip`,
`transpose=90|180|270`, `rotate=DEG[:color]` (arbitrary angle, canvas expands).

**Colour** — `grayscale`, `invert`, `sepia`, `brightness=V`, `contrast=V`,
`saturation=V`, `gamma=V`, `hue=DEG`, `threshold=V`, `opacity=V`, `tint=color`.

**Convolution** — `blur=R` (box), `gblur=SIGMA` (separable Gaussian),
`sharpen[=AMOUNT]`, `edge` (Sobel), `emboss`, `convolution=K[:DIV:BIAS]`
(custom N×N kernel, e.g. `convolution=0 -1 0 -1 5 -1 0 -1 0`).

**Composite / draw** — `overlay=X:Y[:INDEX]` (alpha "over" compositing of
another `-i` input), `fill=color`, `drawbox=X:Y:W:H:color[:fill|thickness]`.

## Examples

```sh
# Thumbnail, preserving aspect ratio
imgcli -i photo.jpg -vf "scale=400:-1" thumb.png

# Stylise: desaturate a touch, boost contrast, soften
imgcli -i photo.jpg -vf "saturation=0.6,contrast=1.15,gblur=1" look.jpg

# Watermark a logo in the top-left, 60% opacity, then convert to JPEG
imgcli -i page.png -i logo.png -vf "opacity=0.6,overlay=24:24" out.jpg

# Rotate 30° onto a transparent canvas
imgcli -i sticker.png -vf "rotate=30:transparent" rotated.png

# No input file? Generate a test card.
imgcli -i testsrc=640x480 card.png
```

## Layout

```
src/image.{h,c}    Image (RGBA frame) + load/save (stb glue, PPM writer)
src/filters.{h,c}  filtergraph parser + every filter + registry
src/source.{h,c}   synthetic input generators
src/util.{h,c}     colour / size parsing
src/main.c         CLI argument handling
third_party/       vendored stb_image.h, stb_image_write.h (public domain)
```

## License

The imgcli source is yours to use freely. The vendored stb headers are
public domain (see their headers).

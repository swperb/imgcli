# AGENTS.md — using imgcli from an agent

Token-economical guide for AI agents and scripts. imgcli is a single, no-dependency
C binary that converts and processes images. It is deterministic, non-interactive,
and emits machine-readable JSON.

## One-line mental model

```
imgcli [-i INPUT]... [-vf "FILTER,FILTER,..."] [-q N] -y [--json] OUTPUT
```
Output format = OUTPUT's extension (png, jpg, bmp, tga, ppm). Filters run left→right.

## Always do this in automation

- Pass **`-y`** (overwrite without prompting) and **`--json`** (parseable result).
- imgcli **never prompts** and **never uses the network**. One process per conversion.

## Build / install (once)

```sh
make && sudo make install      # -> imgcli on PATH ; needs only a C compiler + libm
```

## JSON contract

Success (stdout, exit 0):
```json
{"ok":true,"output":"out.png","width":256,"height":171,"format":"png","bytes":34122}
```
`-info --json` (stdout, exit 0):
```json
{"ok":true,"inputs":[{"path":"in.jpg","width":4000,"height":3000,"channels":4}]}
```
Error (stderr, exit ≠ 0):
```json
{"ok":false,"error":"cannot open input 'x.png': ..."}
```

## Exit codes

| code | meaning |
| --- | --- |
| 0 | success |
| 1 | runtime error (decode / filter / write) |
| 2 | usage error (bad or missing arguments) |

## Recipes (intent → command)

| Intent | Command |
| --- | --- |
| Convert format | `imgcli -y -i in.png out.jpg` |
| Resize to width 800, keep aspect | `imgcli -y -i in.jpg -vf "scale=800:-1" out.jpg` |
| Resize to exact box | `imgcli -y -i in.jpg -vf "scale=512:512" out.png` |
| Make a thumbnail | `imgcli -y -i in.jpg -vf "scale=256:-1" -q 80 thumb.jpg` |
| Crop centre 400×400 | `imgcli -y -i in.jpg -vf "crop=400:400" out.png` |
| Rotate 90° | `imgcli -y -i in.jpg -vf "transpose=90" out.png` |
| Grayscale | `imgcli -y -i in.jpg -vf "grayscale" out.png` |
| Compress JPEG (quality 60) | `imgcli -y -q 60 -i in.png out.jpg` |
| Blur | `imgcli -y -i in.jpg -vf "gblur=3" out.png` |
| Overlay a logo at 20,20 | `imgcli -y -i bg.png -i logo.png -vf "overlay=20:20" out.png` |
| Probe dimensions only | `imgcli --json -info -i in.jpg` |
| Chain several ops | `imgcli -y -i in.jpg -vf "scale=1024:-1,grayscale,contrast=1.2,gblur=1" out.png` |

Get the authoritative, current filter list (29 filters, all chainable):
```sh
imgcli -filters           # human-readable
imgcli -filters --json    # machine-readable: [{"name","syntax","description"}, ...]
```
Use `-filters --json` to enumerate or validate available filters programmatically —
it's generated from the binary itself, so it's always in sync with your version.

## Notes for reliable use

- `-1` in `scale=W:H` preserves aspect ratio on that axis.
- Colors take `#rrggbb[aa]`, `0x...`, `r-g-b[-a]`, or names (`red`, `transparent`) — **no commas** (commas separate filters).
- Inputs may be files **or** generators (`testsrc=WxH`, `color=NAME:WxH`, `gradient=WxH`, `checker=WxH`) — handy for tests with no input file.
- Safety caps: max 16384 px/axis, 64 Mpx total (a clear error if exceeded). See `SECURITY.md`.

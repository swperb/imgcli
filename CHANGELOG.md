# Changelog

All notable changes to imgcli are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [0.5.0] - 2026-06-16

### Added
- **Man page** — a proper `imgcli.1` (and a `make install-man` target, also pulled
  in by `make install`) so distro packagers get the `man imgcli` page they expect.

### Documentation
- README: sharpened the headline ("ImageMagick's power, ffmpeg's syntax, zero
  dependencies"), surfaced the benchmark numbers (~8× faster resize/filter, 232 KB
  vs ~109 MB) in a Benchmarks section near the top, and noted that AVIF/JPEG XL
  decoding stays out of the dependency-free default by design.

## [0.4.0] - 2026-06-16

### Added
- **Batch processing** — `--out-dir DIR` processes every input independently and
  writes one output per input; `-i` patterns are glob-expanded. Per-file errors
  are reported but don't abort the batch (`--fail-fast` to opt out), `--json`
  emits a results array, and the exit code is non-zero if any file failed.
- **stdin/stdout pipes** — `-i -` reads an image from stdin and `OUTPUT` `-`
  writes encoded bytes to stdout, with `-f FMT` to name the format (required for
  stdout). When piping, the result line goes to stderr so stdout stays clean.
- **High-quality resampling** — `scale=W:H:bicubic` (Catmull-Rom) and
  `scale=W:H:lanczos` (Lanczos-3), separable with edge-clamping and downscale
  anti-aliasing. `bilinear` remains the default.

### Changed / internal
- **Integration test suite** (`make test`) — golden pixel output, format
  round-trips, exit codes, and `--json` schema, run in CI on Linux and macOS.
- Supply-chain hardening: Flawfinder C scan, CodeQL config (vendored code
  excluded), and output files created `0644`. CodeQL: 0 open alerts.

## [0.3.0] - 2026-06-15

### Added
- **QOI read/write** — the lossless, zero-dependency "Quite OK Image" format,
  via a vendored public-domain (MIT) `qoi.h`. Detected by magic on read; chosen
  by the `.qoi` extension on write. Decode is dimension-capped before allocation,
  same as the stb path.
- **`--dry-run`** — validate a filtergraph and report the output dimensions/format
  as JSON without writing any file.
- **`-filters --json`** — emit the filter catalogue as machine-readable JSON
  (`[{"name","syntax","description"}, ...]`), generated from the binary so it is
  always in sync.

### Changed
- The MCP server (`mcp/`, published separately as `imgcli-mcp`) gained a
  Streamable HTTP transport and is distributed on Smithery as a local connector.

## [0.2.0] - 2026-06-15

### Added
- Initial public release. ffmpeg-style filtergraph (`-vf "scale=800:-1,grayscale,gblur=2"`)
  with 29 chainable filters across geometry, colour, convolution, and compositing.
- Synthetic sources (`testsrc`, `color`, `gradient`, `checker`).
- Built-in formats with no system libraries: PNG, JPEG, BMP, TGA, GIF, PPM.
- Agent/automation UX: `--json` results, `--version`, `--quiet`, stable exit
  codes (0/1/2), non-interactive operation.
- Security hardening: pre-decode dimension caps, overflow-safe allocation,
  compiler/linker hardening, ASan/UBSan + libFuzzer in CI.

[0.5.0]: https://github.com/swperb/imgcli/releases/tag/v0.5.0
[0.4.0]: https://github.com/swperb/imgcli/releases/tag/v0.4.0
[0.3.0]: https://github.com/swperb/imgcli/releases/tag/v0.3.0
[0.2.0]: https://github.com/swperb/imgcli/releases/tag/v0.2.0

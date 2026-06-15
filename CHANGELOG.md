# Changelog

All notable changes to imgcli are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

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

[0.3.0]: https://github.com/swperb/imgcli/releases/tag/v0.3.0
[0.2.0]: https://github.com/swperb/imgcli/releases/tag/v0.2.0

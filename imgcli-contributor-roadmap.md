# imgcli — Contributor Issues & Growth Roadmap

---

## Actionable Issues for Contributors

Each item below is scoped as a standalone GitHub issue. Difficulty labels are approximate.

---

### 🔧 Feature Issues

---

**Issue: Add WebP read/write support**
`enhancement` `good first issue`

imgcli currently supports PNG, JPEG, BMP, TGA, GIF, and PPM. WebP is now one of the most common image formats on the web and is a notable gap.

**Tasks:**
- Evaluate vendoring `libwebp` (Apache 2.0) or `stb_image` WebP plugin for decode
- Implement WebP encode path via `libwebp` or a lightweight alternative
- Add `.webp` extension detection in `img_save` / `img_load`
- Add WebP to the README format table and test suite

**Acceptance criteria:** `imgcli -i in.png out.webp` and `imgcli -i in.webp out.png` both work and round-trip correctly.

---

**Issue: Add AVIF/HEIC decode support (read-only)**
`enhancement`

AVIF is increasingly common as a delivery format, especially from iPhones and modern web pipelines. At minimum, agents and developers need to be able to *read* these files and convert them.

**Tasks:**
- Evaluate `libavif` (BSD) for AVIF decode
- Implement read-only support and convert path (e.g. `imgcli -i in.avif out.png`)
- Document format support limitations in README

**Note:** Write support is a separate, more complex issue.

---

**Issue: Support stdin as input (`-i -`) and stdout as output**
`enhancement`

Right now imgcli requires named files, which breaks Unix pipeline composability. Supporting `-i -` for stdin and `-` as the output path (piping to stdout) would make it dramatically more useful in shell pipelines.

**Tasks:**
- Detect `-` as the input path in argument parsing and read from `stdin`
- Detect `-` as the output path and write to `stdout`
- Infer format from `-vf` or an explicit `-f FORMAT` flag when extension is unavailable
- Update AGENTS.md with pipe examples

**Example target UX:**
```sh
curl -s https://example.com/photo.jpg | imgcli -i - -vf "scale=400:-1,grayscale" - > thumb.png
```

---

**Issue: Add batch mode for processing multiple files**
`enhancement`

Processing a glob of files currently requires spawning one process per file. A batch mode would cut overhead and simplify automation.

**Tasks:**
- Add a `-batch` flag (or accept multiple `-i` files with glob expansion) with a corresponding output pattern (e.g. `out_%d.png`)
- JSON output in batch mode should emit one result object per line (NDJSON)
- Document in AGENTS.md

**Example target UX:**
```sh
imgcli -y --json -batch "*.jpg" -vf "scale=512:-1" "thumb_%n.jpg"
```

---

**Issue: Expose filter list and filter metadata as JSON (`-filters --json`)**
`enhancement` `good first issue`

`imgcli -filters` currently prints a human-readable list. Agents and build tools that want to validate or enumerate available filters have no machine-readable way to do this.

**Tasks:**
- Implement `imgcli -filters --json` that emits a JSON array of filter descriptors
- Each descriptor should include: `name`, `description`, short `syntax`, and parameter names/types
- Ensure the filter registry in `filters.c` is the single source of truth

**Example output:**
```json
[
  { "name": "scale", "syntax": "scale=W:H[:method]", "params": ["width","height","method"] },
  { "name": "gblur", "syntax": "gblur=SIGMA", "params": ["sigma"] }
]
```

---

**Issue: Add `--dry-run` flag for filtergraph validation**
`enhancement`

Agents need a way to validate a filtergraph without writing an output file. A dry run should parse the graph, validate filter names and parameters, and return what the output dimensions would be.

**Tasks:**
- Implement `--dry-run` flag that runs parse + validation but skips encode/write
- `--json --dry-run` should return `{"ok":true,"width":N,"height":N,"format":"png"}` on success
- Filter errors should surface in the JSON error object with the offending filter name

---

**Issue: Expand `probe_image` / `-info` to return richer metadata**
`enhancement`

The current `-info --json` output only returns dimensions. Agents need more context to make smart processing decisions.

**Tasks:**
- Add to `-info --json` output: `has_alpha`, `color_space` (rgb/gray), `bit_depth`, `file_size_bytes`, and EXIF fields where available (orientation, camera make/model, capture date)
- Evaluate vendoring a minimal EXIF reader (e.g. `stb_image` already reads basic EXIF in some builds)
- Document new fields in AGENTS.md

---

**Issue: Expose discrete MCP tools instead of raw filtergraph passthrough**
`enhancement` `agent-ux`

The current MCP server exposes `convert_image` which accepts a raw `-vf` filtergraph string. Agents compose DSL strings poorly and reliably. Individual structured tools would be far more reliable.

**Tasks:**
- Add MCP tools: `resize_image`, `crop_image`, `rotate_image`, `apply_color_filter`, `composite_images`, `convert_format`
- Each tool takes typed, named parameters (e.g. `width`, `height`, `filter_name`, `amount`)
- The MCP server assembles the filtergraph internally
- Keep `convert_image` as a power-user escape hatch
- Update `mcp/README.md`

---

**Issue: Normalize all error messages to JSON when `--json` is passed**
`bug` `good first issue`

Filter parse errors and some argument errors currently go to stderr as plain text even when `--json` is passed. This breaks agent error handling.

**Tasks:**
- Audit all `fprintf(stderr, ...)` paths in `main.c` and `filters.c`
- When `--json` is active, all errors should emit `{"ok":false,"error":"..."}` to stderr instead
- Add the offending filter name/argument to the error object where applicable
- Add a regression test

---

### 📖 Documentation Issues

---

**Issue: Add a `CONTRIBUTING.md`**
`documentation` `good first issue`

There's no contributor guide. New contributors have no idea how to build, test, or structure a PR.

**Tasks:**
- Document build requirements (C11 compiler, `make`, optional ASan/fuzz targets)
- Describe how to add a new filter (the `filters.c` registry pattern)
- Note coding style expectations
- Describe how to run the fuzz harness

---

**Issue: Add integration test suite**
`testing`

There are no visible automated integration tests beyond the fuzz harness.

**Tasks:**
- Write a shell-based test suite (or use a lightweight C test runner) covering: format conversion round-trips, each filter with known-good output hashes, error/exit-code paths, and JSON output structure validation
- Hook into `make test`
- Add to CI (GitHub Actions)

---

## Getting imgcli Widely Used

The technical quality is there. The gap is discoverability, trust, and distribution. Here's what would move the needle:

---

### 1. Solve the WebP/AVIF gap first

Before any marketing push, WebP support is table stakes. It's the first thing a new user will try and the first reason they'll bounce. This is the highest-leverage technical issue.

---

### 2. Publish to more package managers

Currently only Homebrew and AUR. To reach meaningful developer adoption:

- **`apt` / Debian PPA** — reaches the largest Linux server user base
- **`winget` / Scoop** — Windows developers are underserved in this space
- **`nix` / nixpkgs** — the Nix community values exactly the dependency-free, reproducible-build ethos imgcli embodies
- **Docker Hub official image** — a `docker run imgcli` one-liner is compelling for CI use cases

---

### 3. Make the agent story the lead, not a footnote

The "agent-native image tool" angle is genuinely differentiated right now. ImageMagick and vips have no `--json`, no AGENTS.md, no MCP server. Lean into this:

- Write a blog post or README section: *"Why imgcli is the right image tool for AI agents"*
- Publish the MCP server to the MCP registry so it shows up when agents search for image tools
- Post to the Claude, OpenAI, and LangChain community forums with a concrete agent workflow example

---

### 4. Benchmark against ImageMagick

A credible, reproducible benchmark showing imgcli is faster or comparable on common operations (thumbnail generation, format conversion at scale) would be shared widely. The `bench/` directory suggests this is already on the author's mind — publishing results publicly would be high-impact.

---

### 5. Get into CI pipelines

The zero-dependency, single-binary story is perfect for CI. A concrete GitHub Actions example that uses imgcli to resize/optimize images on PR (e.g. for documentation or assets) would drive organic adoption. Submit it to GitHub's Actions Marketplace or awesome-actions lists.

---

### 6. Write one great tutorial

A single well-written post — *"Processing 10,000 product images with imgcli and 10 lines of shell"* — on a platform like dev.to, Hacker News, or the GitHub blog would likely be the highest-ROI marketing activity available at this stage. Zero-dependency + fast + composable is a story that resonates with the HN crowd.

---

### 7. Tighten the feedback loop for contributors

Right now there's no `CONTRIBUTING.md`, no test suite, and 10 open issues with 4 open PRs. For a new contributor, this is uncertain territory. Closing that loop — clear contributing guide, CI green on every PR, issues labeled and triaged — is what turns one-time contributors into regulars.

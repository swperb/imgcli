# Security Policy & Threat Model

imgcli decodes **untrusted image files** in C, so it takes memory safety
seriously. This document is the result of an end-to-end audit against the OWASP
Top 10, common C vulnerability classes (CWE), and the historical vulnerability
classes of ffmpeg (the closest large-scale prior art for "process untrusted
media in C"). It records what applies, what doesn't, and what we do about it.

## Reporting a vulnerability

Please open a **private** security advisory on the GitHub repository (Security →
Report a vulnerability), or email the maintainer. Do not file public issues for
exploitable bugs. We aim to acknowledge within a few days.

## Threat model & trust boundary

| Aspect | imgcli |
| --- | --- |
| **Untrusted input** | the bytes of the input image file(s) — the primary attack surface |
| **Trusted input** | the command line itself (filtergraph, paths, flags) — supplied by the operator |
| **Outputs** | an encoded image file written to an operator-chosen path |
| **No network** | imgcli never opens sockets, fetches URLs, or resolves remote resources |
| **No subprocesses** | it never `exec`s, loads plugins, or does dynamic format loading |
| **No protocols/containers** | it reads a single local pixel-data file; file contents are never treated as a pointer to other resources |

The decode step (stb_image) is where attacker-controlled bytes meet C parsing,
so it is treated as the hot zone: capped, fuzzed, sanitized, and kept current.

## OWASP Top 10 (2021) applicability

OWASP's Top 10 targets web apps; for a local CLI most items are out of scope.
The relevant ones:

| OWASP | Applies? | Posture |
| --- | --- | --- |
| A01 Broken Access Control | n/a | no auth/multiuser surface; respects filesystem permissions |
| A03 Injection | **partial** | no shell/SQL. We never pass user data as a `printf` **format string** (no format-string injection); paths are passed to `fopen`/`stb`, never to a shell |
| A04 Insecure Design | addressed | narrow scope by design: no network/protocol/subprocess features = whole vuln classes excluded |
| A05 Security Misconfiguration | addressed | non-interactive & deterministic; will not silently overwrite (`-y` required); hardening flags on by default |
| A06 Vulnerable & Outdated Components | **key item** | the vendored stb headers are the real dependency risk — see "Dependency policy" |
| A08 Software & Data Integrity Failures | addressed | dependencies are vendored (no build-time network fetch); pin & review on update |
| A09 Logging/Monitoring | n/a | batch tool; errors go to stderr, machine-readable with `--json` |
| A02/A07/A10 (crypto, auth, SSRF) | n/a | no crypto, no auth, no server-side request capability |

> **A10 SSRF / arbitrary-file-read** is ffmpeg's most dangerous real-world class
> (crafted `.m3u8`/HLS playlists referencing `file://`/`http://`). It is
> **structurally impossible** in imgcli: there is no protocol/playlist handling.

## Common C vulnerability classes (CWE)

| CWE | Class | Status in imgcli |
| --- | --- | --- |
| CWE-190 | Integer overflow → undersized alloc | **Mitigated.** Every frame is allocated through one choke point (`img_alloc`) guarded by `img_dims_ok()`; `W*H` is capped (`IMG_MAX_PIXELS`) so `W*H*4` cannot overflow `size_t`. Geometry filters validate target dims before allocating. |
| CWE-787 / CWE-125 | OOB write / read | Filters sample with edge **clamping** (`sample_clamp`); all buffer indices derive from validated dims. Decode OOB risk lives in stb (fuzzed + pinned). |
| CWE-400 / CWE-409 | Resource exhaustion / decompression bomb | **Mitigated.** Dimensions are read from the header (`stbi_info`) and rejected **before** pixel decode; `STBI_MAX_DIMENSIONS` also caps stb internally. Caps: 16384 px/axis, 64 Mpx total (overridable at build time). |
| CWE-369 | Divide by zero | Guarded: convolution divisor defaults away from 0; scale divides by source dims (≥1); decode return is NULL-checked. |
| CWE-416 / CWE-415 | Use-after-free / double-free | Clear single-owner lifetimes; the filtergraph frees each intermediate exactly once. Verified leak-free (`leaks`) and clean under ASan. |
| CWE-134 | Format string | **None.** User/-derived strings are always passed as `%s` arguments, never as the format. |
| CWE-476 | NULL deref | Decode/alloc returns are checked everywhere. |
| CWE-78 | OS command injection | n/a — imgcli never invokes a shell or subprocess. |

## ffmpeg vulnerability classes vs. imgcli

ffmpeg has had hundreds of CVEs since the early 2000s. Mapping the recurring
*classes* onto imgcli's design:

| ffmpeg class (example) | Applies to imgcli? | Why / mitigation |
| --- | --- | --- |
| Heap/stack overflow in decoders (e.g. CVE-2026-39210/39217) | **Via stb, not us** — we have no decoders of our own | Fuzz + pin stb; ASan/UBSan in CI |
| Integer overflow → undersized alloc (e.g. CVE-2022-2566) | **Yes — our own size math + stb** | Checked, capped allocation (CWE-190 row above) |
| OOB read / info leak (e.g. stb CVE-2019-15058) | **Via stb** | Force `req_comp=4` (no channel-count assumptions); fuzz; pin |
| Use-after-free / double-free (e.g. CVE-2025-59734) | **Low in our code; via stb possible** | Simple single-threaded lifetimes; ASan; pin stb |
| Div-by-zero / DoS (e.g. CVE-2019-13390) | **Yes (low severity)** | Divisor/return guards |
| Decompression bomb / unbounded recursion | **Yes** | Pre-decode dimension cap; bounded filters |
| Type confusion (e.g. stb CVE-2021-42716 PNM) | **Partial** | Always normalize to RGBA8; honor stb's reported format; pin |
| **SSRF / arbitrary file read via protocols/HLS** (CVE-2024-36613) | **No — by design** | No network, no protocols, no playlists |
| Subprocess / plugin / dynamic loading | **No — by design** | None present |

**The real attack surface is stb_image, not ffmpeg's decoders.** Notably, the
formats imgcli supports — GIF, TGA, PNM/PPM, JPEG — are exactly the ones with
historical stb_image CVEs (CVE-2018-16981 GIF, CVE-2019-15058 TGA,
CVE-2021-42716 PNM, CVE-2023-45661/45663/45664 GIF/double-free, plus a recent
CVE-2026-5186 UAF report). Hardening budget is spent accordingly.

## Mitigations implemented

- **Pre-decode dimension cap.** `img_load` calls `stbi_info` (header only) and
  rejects oversized images *before* allocating pixel memory — the key
  decompression-bomb defense. `STBI_MAX_DIMENSIONS` is set as a second line.
- **Centralized, capped, overflow-safe allocation** via `img_alloc` +
  `img_dims_ok` (`IMG_MAX_DIM`, `IMG_MAX_PIXELS`).
- **Channel normalization.** Always decode with `req_comp=4` → no bit-depth /
  channel-count confusion in downstream code.
- **No format strings, no shell, no network, no plugins.**
- **Compiler/linker hardening** (see `Makefile`): `-D_FORTIFY_SOURCE=2`,
  `-fstack-protector-strong`, `-fPIE` (+ `-pie`, RELRO/now, noexecstack,
  `-fstack-clash-protection` on Linux), `-Wformat -Wformat-security`.
- **Sanitizers:** `make asan` (ASan + UBSan). Run across the test battery and an
  adversarial corpus (truncated/garbage/bomb headers) with zero findings.
- **Fuzzing:** `make fuzz` builds a libFuzzer harness over
  decode → filtergraph; `make fuzz-replay` runs the same harness over a corpus
  on platforms without the libFuzzer runtime.
- **Leak-checked:** clean under macOS `leaks` across all paths.
- **Deterministic & non-interactive:** never prompts; refuses to overwrite
  without `-y`; `--json` gives structured, parseable results and errors.

## Dependency policy (the most important ongoing control)

imgcli vendors `third_party/stb_image.h` **v2.30** and
`third_party/stb_image_write.h` **v1.16** (public domain). Because these are the
real attack surface:

- Watch [nothings/stb](https://github.com/nothings/stb) for security fixes.
- Update deliberately, re-run `make asan` + `make fuzz`, and note the version
  bump in the changelog.
- Only the needed formats are compiled in; unused decoders can be removed with
  `STBI_NO_*` defines to further shrink the surface.

**Default build = zero third-party dependencies** (links only `libc`); the
vendored stb headers are compiled in, not linked. Formats that cannot be handled
without an external library — **WebP, AVIF, HEIC** — are supported (if at all)
only behind **opt-in build flags** (e.g. `make WEBP=1`) that link libwebp /
libavif / libheif. Those libraries are **never** part of the default binary, so
the default build's dependency-free and attack-surface guarantees are unchanged.
Enabling an opt-in flag adds that library to your trust/attack surface — review
and update it like any other dependency.

## Residual risk & honesty

- **C is memory-unsafe.** Per NSA/CISA guidance, memory-safe languages eliminate
  whole bug classes that C cannot. imgcli stays in C for zero-dependency
  portability and compensates with the layered controls above (caps, checked
  math, hardening flags, ASan/UBSan, fuzzing) — defense-in-depth, not a
  guarantee. A from-scratch or Rust rewrite of the decode path would be the next
  step for a higher assurance bar.
- **stb_image bugs are inherited.** Our strongest control is pinning + tracking
  upstream and fuzzing; a novel stb 0-day would affect imgcli until patched.
- If imgcli output is returned to an untrusted party, treat a decoder over-read
  as a potential heap-memory disclosure vector — keep stb current.

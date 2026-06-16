# Contributing to imgcli

Thanks for helping improve `imgcli`. This project keeps the tool small, deterministic, and dependency-free, so contributions should stay tightly scoped and easy to review.

## Prerequisites

You only need a C11 toolchain, `make`, and `libm` to build the main binary:

```sh
make
```

For the extra validation targets:

- `make asan` needs a compiler with AddressSanitizer and UndefinedBehaviorSanitizer support.
- `make fuzz CC=clang` needs `clang` with the libFuzzer runtime available.
- `leaks` checks run in CI on macOS; you do not need them locally unless you want to reproduce that job.

## Local development workflow

Start with a normal build, then run the smoke test that CI uses:

```sh
make
make check
```

Before opening a PR, run the checks that match your change:

- `make check` for every change.
- `make test` — the integration suite (golden pixel output, format round-trips,
  exit codes, `--json` schema). If a change intentionally alters output, refresh
  the references with `make test-update` and commit the updated `tests/golden/`.
- `make asan` if you touched image parsing, filter execution, buffer sizing, or other memory-sensitive code.
- `make fuzz CC=clang` if you changed decode or filtergraph behavior and have a libFuzzer-capable `clang`.

If a validation target is unavailable in your environment, mention that in the PR so maintainers know what you could and could not verify locally.

## Adding or changing a filter

Most filter work lives in [`src/filters.c`](src/filters.c).

1. Add the filter implementation as a `static Image *f_name(...)` function.
2. Follow the existing calling convention:
   - return `in` for in-place filters
   - allocate and return a new `Image *` for geometry-style filters
   - return `NULL` and set `*err` on failure
3. Register the filter in the `FILTERS[]` table near the bottom of `src/filters.c`.
4. Add a short usage string there so `imgcli -filters` stays accurate.
5. Document it: add the new filter to the **Filters** list in [`README.md`](README.md)
   under its family (Geometry / Colour / Convolution / Composite), and update any
   examples it affects. A new filter is user-facing, so this step is required.

The filtergraph parser is intentionally simple: filters are chained left-to-right, arguments are colon-separated, and commas separate filters. Keep new filter syntax consistent with that model.

## Coding style

Match the existing style in `src/`:

- use C11-compatible code
- prefer small `static` helper functions over broad refactors
- keep comments short and explanatory, especially around safety-sensitive logic
- preserve the current brace style, indentation, and compact error-handling patterns
- reuse existing helpers such as `seterr`, `argd`, `argi`, `args`, and `img_dims_ok` before adding new abstractions

Please avoid adding runtime dependencies unless the change clearly justifies the portability and maintenance tradeoff.

## Pull requests

PRs should stay focused and explain the user-visible impact.

Before opening a PR:

- make sure CI-relevant checks are green locally where possible
- include tests or smoke coverage when behavior changes
- mention any platform-specific limitations or skipped checks

When writing the PR description, include:

- what changed
- why it changed
- how you validated it locally

The repository uses branch protection and CI gating on `main`, so expect the pull request checks to pass before merge.

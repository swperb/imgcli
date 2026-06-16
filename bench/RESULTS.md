# Benchmark results — imgcli vs ImageMagick

Reproduce these yourself: `make && bench/bench.sh` (with `magick` on PATH).

**Environment:** Apple Silicon (arm64), macOS (Darwin 25.5.0) · imgcli 0.2.0 ·
ImageMagick 7.1.2-25 Q16-HDRI. **Method:** wall-clock over N iterations ÷ N
(`/usr/bin/time`), steady-state (warm cache). Numbers vary by machine, image,
and operation — the script is the source of truth.

## Speed (mean time per invocation, lower is better)

| Operation | imgcli | ImageMagick | imgcli speedup |
| --- | ---: | ---: | ---: |
| Small PNG→JPEG + resize (one-shot, N=100) | **1.6 ms** | 4.7 ms | **2.9×** |
| Resize 1200×1200 → 300w PNG (N=25) | **6.0 ms** | 51.6 ms | **8.6×** |
| Grayscale 1200×1200 (N=25) | **48.4 ms** | 389 ms | **8.0×** |

> First-invocation (cold) gaps are larger still — the very first `magick` call in
> a fresh shell measured ~17 ms vs imgcli's ~1.6 ms, which is the realistic shape
> for an agent/script that spawns one process per task.

## Footprint (install size & dependencies, smaller is better)

| | imgcli | ImageMagick |
| --- | ---: | ---: |
| On-disk install | **232 KB** | ~109 MB |
| Third-party dependencies | **0** (libc only) | 17 packages |
| Dynamically linked libraries | **1** (`libSystem`) | 10 |

imgcli is ~**470× smaller** on disk and ships as a single self-contained binary.

## Honest caveats (so you can trust the wins above)

- **ImageMagick does far more.** It defaults to 16-bit/HDRI precision with colour
  management, supports 200+ formats, and offers higher-quality resampling
  (Lanczos, etc.). imgcli is a lean **8-bit RGBA** pipeline (now with bicubic/lanczos resampling too).
- Part of imgcli's speed is **doing less, more directly** — which is exactly the
  point of a small single-purpose tool. For maximum fidelity, format breadth, or
  advanced filters, reach for ImageMagick. For fast, lightweight, scriptable, and
  agent-friendly convert/resize/filter, imgcli wins on every axis measured here.
- Benchmarks are single-threaded simple operations; your mileage will vary.
  Re-run `bench/bench.sh` on your hardware.

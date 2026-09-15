# PERFORMANCE — temporal-intelligence-11

## Resource model

- Build: Ninja `-j2` (CMAKE_BUILD_PARALLEL_LEVEL=2), MSVC Debug, single configure ~N min (vcpkg restore cached in `build-dev/vcpkg_installed`).
- Tests: `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`.
- Compilation monitoring: per prompt, if per-60s CPU/RSS sampling is not measurable in this shell, record once here and keep `-j2` cap.
  - 2026-09-15: Git Bash on Windows host without a portable RSS sampler for cl.exe subprocesses; recording this once per envelope rule; `-j2` cap maintained for all builds; any degradation signal (build stall/OOM) → drop to `-j1`.

## Logical scale (correctness gates, not wall-clock)

- Kernel tests: series length 48–400 samples; corpus ≤ 64 scenarios per test binary — bounded.
- Operator E2E: fixtures of ≤ 8×8 pixels × ≤ 48 dates (existing TestScene pattern) — bounded.
- New operators declare `executionEstimate` costClass and reuse tile-based streaming (`tile_size` param default from existing operators); region guard R×T ≤ 50M cells (existing region_features guard) applies to any per-region fit loop; per-region series length bounded by collection size (existing 2 GiB gather guard in phenology).
- Bootstrap: default 199 resamples × O(T·k²) per fit — bounded and opt-in; hard cap param clamped (e.g. ≤ 999).

## Memory bounds

- New kernels: O(T) per series + O(k²) Gram; scratch reuse (G) removes per-pixel allocations; no T×H×W materialization anywhere (TemporalTileReader contract).
- Bootstrap CI: O(B·k) coefficient storage or streaming quantiles — B capped; per-pixel loop reuses scratch.

## Benchmark policy

`tests/benchmark_temporal10.cpp` remains evidence-only. This track reports logical-scale and allocation-removal evidence (bit-exact outputs + code structure), never wall-clock as a gate. A before/after allocation count for the refit path may be recorded in EVIDENCE if trivially obtainable.

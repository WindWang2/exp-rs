# Task Plan - Large-Scale Remote Sensing Raster Performance Engineering

## Goal Overview
Transform `exp-rs` from full-raster, unscalable memory consumption and static resource estimates to memory-bounded streaming block processing, optimized GDAL I/O, data-dependent resource estimation, and verified throughput/RSS benchmark convergence.

## Environment Baseline
- **OS**: Arch Linux (x86_64)
- **CPU**: AMD Ryzen 9 5900HX (8 cores / 16 threads, up to 4.68 GHz)
- **RAM**: 62 GiB Total (~45 GiB available)
- **GDAL**: 3.13.2 "Iowa City"
- **Worktree**: `~/projects/rs-studio/wt-rs-performance-20260809`
- **Branch**: `agent/20260809-rs-performance`

## Phase Roadmap

### Phase 0: Baseline & Environment Setup
- [x] Worktree & Branch isolation verification
- [x] Collect OS, CPU, RAM, GDAL system specs
- [x] Initial build setup and baseline test execution
- [x] Create `.planning/agent-rs-performance/` tracking documents

### Phase 1: Raster Operator Inventory & Hotspot Ranking
- [x] Scan codebase for raster processing, operators, algorithms, execution estimates, and `FullRaster` usage.
- [x] Inventory memory policy, execution estimates, input/output counts/dimensions/bands, data types, pass count, GDAL `RasterIO` call patterns, threading, scratch disk usage.
- [x] Rank operators by `Potential Peak RSS x Call Frequency x User Value`.

### Phase 2: First Vertical Slice - Image Fusion Framework
- [x] Audit existing Image Fusion implementations (`linear`, `Brovey`, `IHS`, `PCA`, `Gram-Schmidt`).
- [x] Implement block/tile/stripe streaming interface and processing pipeline for local fusion algorithms (`linear`, `Brovey`, `IHS`).
- [x] Update `RsImageFusionOperator` memory policy to `RSOperatorMemoryPolicy::Streaming` and set dynamic 12 MiB tile working set memory estimation.
- [/] Benchmark Image Fusion on small, medium, and large synthetic rasters to confirm bounded Peak RSS.

### Phase 3: Dynamic Resource Estimate Framework & Memory Budgeting
- [x] Connect task resource estimation (`executionEstimate()["estimatedRamBytes"]`) to admission control and system RAM budget protection in `ResourceMonitor`.
- [x] Upgrade `RSExecutionEstimate` and `MemoryPolicy` from declarative metadata to data-dependent resource models.
- [ ] Add saturation-safe / overflow-checked arithmetic (`size_t` / integer overflow protection).
- [ ] Integrate with system scheduler to enforce processing memory budget, preventing CPU/thread oversubscription and RAM saturation.

### Phase 4: Extended High-Risk Operators & GDAL I/O Optimizations
- [ ] Optimize next highest ROI operators based on inventory (PCA, Spectral Unmixing, RX Anomaly, Hyperspectral Transforms).
- [ ] Audit GDAL I/O patterns: block-aligned `RasterIO`, avoiding duplicate band reads/reopens, cache interactions, output flushing.

### Phase 5: Comprehensive Benchmark Harness & Numerical Correctness Verification
- [x] Build headless benchmark harness (`test_perf_fusion.cpp`) measuring Wall clock time, Peak RSS (`getrusage`), Throughput (MPix/s) on scaling rasters.
- [ ] Validate numerical floating-point equivalence and edge cases across unit test suite.

### Phase 6: Adversarial Performance & Safety Review
- [ ] Run adversarial review checking hidden full-frame allocations, copy overhead, thread oversubscription, temp file leaks, nodata regressions, overflow, buffer lifetimes.
- [ ] Fix all Critical and High findings.

### Phase 7: Completion & PR Submission
- [ ] Run `git diff --check` and verify build + tests.
- [ ] Push branch to remote and create PR to `master`.

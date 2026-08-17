# Task Plan: Remote Sensing Algorithm Core Audit & Fix (`exp-rs`)

## Goal
Audit, reproduce, fix, verify, adversarially review, commit, push, and open PR for remote sensing algorithm core defects across:
1. Algorithm Correctness (spectral, indices, unmixing, classification, clustering, atmospheric, calibration)
2. Numerical Stability (conditioning, zero norms, NaN/Inf handling, non-finite spectra, RNG determinism)
3. NoData / NaN / Mask Platform Contract (metadata vs written value consistency, propagation, multi-band)
4. GDAL Raster / Grid / Affine Correctness (geotransform, pixel corner/center, extent, south-up, rotation, partial overlap, OOB)
5. Large Raster Memory & CPU Hot Paths (O(1) streaming RAM, allocation in tight loops, complexity bottlenecks)
6. Streaming & Cancellation (responsive cancellation, partial output cleanup, resource lifecycle)

## Phase Structure
- [x] **Phase 1: Environment & Baseline Verification**
  - Fetch latest `origin/master` (BASE_SHA: `19843d1b6910c9207c7e5c97863a873db679368e`)
  - Create branch `agy/audit-raster-core-20260815`
  - Fix build setup / versioning in CMake and verify build
- [ ] **Phase 2: Multi-Agent Parallel Audit (Read-Only)**
  - Agent A: GDAL / Raster Grid / Affine / Mask / Overlap / GeoTransform
  - Agent B: Classification / Statistics / Machine Learning / KMeans / Random Forest
  - Agent C: Hyperspectral / Spectral Indices / Unmixing / PPI / SAM / SID / RX / PCA / MNF
  - Agent D: Large Raster / Memory Working Set / CPU Hot Path / Tight Loop Allocations
  - Agent E: Streaming / Cancellation / Resource Leaks / Partial Output
  - Agent F: Skeptic Reviewer / Pre-existing vs Master status
- [ ] **Phase 3: Hard-Problem Escalation (Pro / Opus Reasoning)**
  - Deep dive into P0/P1 issues, cross-module NoData propagation, numerical singularities
- [ ] **Phase 4: Targeted Reproduction & Test-Driven Fixes (TDD)**
  - Write failing test cases (Catch2) for confirmed bugs
  - Implement minimal surgical fixes
  - Verify green tests
- [ ] **Phase 5: Full Verification & Sanitizers / Smoke Benchmarks**
  - Run full test suite
  - Verify equivalence and numerical integrity
- [ ] **Phase 6: Adversarial Independent Review**
  - Review `git diff origin/master...HEAD`
  - Verify all invariants
- [ ] **Phase 7: Atomic Commits & PR Generation**
  - Create focused commits
  - Push branch and create PR (no merge)

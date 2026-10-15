# R14-B Ownership

## Writable (production code)

* `src/operators/runtime/**`
* `src/runtime/gpu/**`
* `src/runtime/chunk/**`
* `src/runtime/exec/**`
* `src/runtime/worker/**`
* `models/**` — existing manifests validation/correction ONLY (no new model lines)

## Writable (tests)

* This track's test files, new ones named `test_r14_B_*`
* Existing B-track-specific test files listed in tests/CMakeLists.txt

## Read-only (other tracks)

* `src/operators/rs/**` (A), `src/python/isolated/**` (C), `src/sdk/**` (C),
  `src/geospatial/**` + `src/data/**` + `src/dataset/**` + `src/experiment/**` (#1504),
  `src/app/**` (D, with E's exceptions), `src/agent/**` (C, with D's exceptions).

## High-conflict short-lease files (avoid; if unavoidable: tiny change, separate commit, recorded here)

* root `CMakeLists.txt`, `tests/CMakeLists.txt` (shared registration),
  `.github/workflows/**`, `scripts/**`, global registries.

## Concurrency with #1505 (open)

`src/runtime/gpu/gpu_plane.cpp` + `tests/test_gpu_plane.cpp` are modified by #1505.
Rule: B may edit `src/runtime/gpu/**` other than duplicating the warm-session eviction;
if `gpu_plane.cpp` itself must change, the change is designed against #1505's
post-merge shape and the PR body documents the interlock.

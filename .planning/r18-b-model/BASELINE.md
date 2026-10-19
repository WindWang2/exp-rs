# R18-B Baseline — 遥感 AI 模型推理：算子运行、GPU/CPU 生命周期与分块恢复

- **Created:** 2026-10-19 (session start ~01:40 UTC)
- **Worktree:** `/home/kevin/project/exp-rs-r18-b-model-runtime`
- **Branch:** `agent/r18-b-model-runtime-perf` (created by `scripts/dev/new_worktree.py`, output recorded in recon)
- **Base:** `origin/master` = `2d2881631fc821537100c00da677888e3a31ea8f` (Merge PR #1533)
- **Task-book snapshot SHA** `7825c1ebd` was **stale** — live recon re-queried everything before any action (per §1).

## Host reality

- ZCode host, Linux x64 (cachyos 7.2.9-1), `gh` authenticated as WindWang2 (full repo scope).
- `/goal-loop` skill NOT installed (`.agents/skills` has no goal-loop; `~/.zcode/skills` empty) → running native ZCode protocol + this task book's Oracle protocol; no slash-command success is claimed.
- Direct `git` to github.com:443 is **flaky** (intermittent TCP connect hangs; curl to same IP works). Fetches may need retries. Recorded because it affects push/PR timing, not correctness.
- Local main checkout was **149 commits behind** origin/master at recon; all work happens in the fresh worktree.
- No CUDA/TensorRT/OpenVINO/ONNX Runtime hardware/deps assumed present — provider lanes that cannot run locally are disclosed as NOT_EXECUTED, never faked.

## Live recon summary (2026-10-19)

- Open PRs: only `#1513` (draft, r15-B geodata: src/geospatial, src/experiment, src/data, src/dataset, src/cli + tests tail) — **zero overlap** with this track's write set.
- Open issues: `#1491` (CI has no completed runs since 2026-10-11 — motivates "do not wait for online CI"), `#1467` (local-modification cluster — other tracks).
- Predecessor tracks (all MERGED): `#1496` (r12-F), `#1508` (r14-B), `#1515` (r15-C). Their fixes are the floor, not the ceiling; this track must find **increments**.
- `overlap_scan.py src/operators/runtime src/runtime/gpu src/runtime/chunk` → overlap_count 0.
- `stale_branches.py` → 26 stale remote branches (none in this track's paths).
- No remote `r18*` or competing `agent/*` branches in this domain.

## Predecessor coverage (what is already fixed — do NOT redo)

From r14-B (#1508) final report + r15-C (#1515) PR body (full lists preserved in PR records):
- decode gates for NaN boxes, batched/square-head decodes, halo misalignment, morphology re-tally, stitched-mask tallies, >512-band channel overflow, multi-input batch mismatch, morphology temp cleanup
- provider fixes: OpenCV shared-buffer race, HTTP dispatcher-hang watchdog, OpenVINO infer mutex + dtype memcpy, TRT thread-local device + binding order + dtype, python worker stale-answer/timeout taxonomy, TensorBlob channel/dtype validity
- detection: half-open tile ownership, maxDetections post-NMS cap, negative classId clamp, dedup key wraparound (r15-C C08)
- publish: companion/sidecar crash-suffix adoption (r15-C C13), park/adopt ladder audited
- VRAM ledger int overflow + phantom holders (r15-C C11), ReduceConcurrency admission census, provider UnavailableReason surfacing (C05), manifest registration cross-constraints (C02), rs:detect conf/nms_iou validation (C14), min_valid_coverage gate (C07), error-code unification, SICNU_MODEL_CUDA_DEVICES bounds (C04), weighted_vote NoData quantization (C09), join progress denominator (C12), platform-10 capability gate assertions (C16)

## Build/verification plan

- Independent build dir `build-r18-b` inside the worktree (no preset hard-coded path reuse), `ENABLE_TESTS=ON`, deps probed by existing project config.
- Concurrency: `min(4, repo cap 2, RAM)` → **-j2** (drop to -j1 under pressure), guarded by the global five-track lock `/tmp/exp-rs-five-tracks-build.lock` via `flock` + `resource_guard.py`.
- Env: `SICNU_BUILD_REQUIRE_GUARD=1`, `SICNU_BUILD_JOBS_CAP=2`, `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`.

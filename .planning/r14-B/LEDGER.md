# R14-B Ledger

## Session log

- **2026-10-15 12:0x** Host = ZCode (Antigravity unavailable; native multi-Agent
  used — see BASELINE.md DEGRADATION). Repo state re-read: origin/master
  f72f0c117, open PRs #1504 (geo, read-only) + #1505 (r13-j; touches
  src/runtime/gpu/gpu_plane.cpp warm-session eviction — dedup rule set).
  #1499 verified FIXED_ON_MASTER (syntax check exit 0). #1455 P1 verified
  fixed-by-#1501 in code (holder tickets) with existing regression test.
  #1459 scratch digest verified fixed-by-#1501 (streamFnv1a). #1447 sites all
  outside B ownership.
- **12:1x** Worktree + branch created (hardening/r14-B-model-runtime @ f72f0c117).
  Build configured (Ninja/Release/tests ON, GDAL from wp2-prefix, Catch2 via
  FETCHCONTENT_SOURCE_DIR override — GitHub git-over-HTTPS unreachable on
  this host; see BASELINE network note).
- **12:2x** Three read-only hostile audits dispatched (tile engine, detection
  stack, provider layer); orchestrator re-verified every accepted finding
  line-by-line before fixing (findings without orchestrator confirmation were
  dropped).
- **12:3x-13:1x** Fixes implemented (Batches A-D): detection decode
  (NaN/batch/auto), NMS+dedup NaN hardening, seam rule half-open, classId
  clamp, maxDetections post-NMS exact cap + 8x raw memory guard, strided-head
  halo crop (single+multi), morphology tally replace + kernel guard + residue
  cleanup + call-site stage cleanup, Mask tally, CV_CN_MAX refusels x3,
  multi-input N==B check, OpenCV inferMulti detach + setInput-in-try +
  failure recording + float32 gates, HTTP dispatcher guard, OpenVINO mutex +
  dtype + typed-error passthrough, TRT mutex + device pin + dtype +
  binding-by-direction + >=10 API port, python live-timeout classification,
  TensorBlob channel axis + overflow gate.
- **13:2x** All edited TUs pass -fsyntax-only via the compile database.
  Three new test files written and syntax-clean; tests/CMakeLists.txt
  additive registration (3 targets). Models/** scan: 26/26 manifests valid
  JSON (WP-11 covered by existing library gate).
- **13:3x** Comprehensive test-target build queued behind the shared
  /tmp/exp-rs-r14-global-build.lock (R14-C track holds it; honored).
  RED/GREEN protocol: stash src fixes → rebuild → RED ctest; unstash →
  rebuild → GREEN ctest ×2; regression suite ×2.

## Decisions

1. **maxDetections**: raw accumulation guard relaxed to 8×cap (memory bound
   rationale in code); exact product cap enforced post-NMS. F-OPS-5 timing
   test unaffected (exercises dedupDetections directly).
2. **HTTP no-dispatcher**: typed refusal rather than helper-thread exchange
   (lifetime complexity); converts an infinite hang into a typed failure;
   helper-thread exchange recorded as follow-up.
3. **TRT ≥10 API port**: ported getNbIOTensors/getTensorShape/getTensorIOMode
   mirroring the already-present ≥10 enqueue calls; compile-unverifiable on
   this host (no CUDA/TRT) — but the ≥10 half cannot compile on master either
   (unguarded getNbBindings), so no regression is possible. Disclosed.
4. **Seam rule**: half-open [x, x+w) ownership; centers exactly on raster
   right/bottom edge are unreachable (decode clamps + drops empty boxes).
5. **strided-head contract**: pinned by tile-count invariance + even-column
   exactness instead of pinning cv::resize interpolation internals.

## Commits

- `c47d8cedb` harden(detection): NaN-safe decode, batch/layout refusals, seam ownership, writer clamps (+test_r14_B_detection_decode)
- `649fa3249` harden(tile-engine): strided-head halo alignment, morphology re-tally + stage hygiene, CV_CN_MAX refusals, multi-input batch check (+test_r14_B_tile_engine)
- `f5888fa2b` harden(providers): shared-session serialization, typed timeouts, TensorBlob contracts (+test_r14_B_runtime_providers + CMake registration)
- `0aa21ec37` docs(r14-B): ledger/oracles/risk/test-results + .gitignore whitelist (repo convention)
- `6e1c9d547` harden(providers): review round-2 fixes (permanentlyDead on python timeout, inferMulti cancel re-check, dtype-gate pin, detachment retitle)
- `78e9ed2ff` docs(r14-B): final report

## Push / PR

- Branch pushed via `git -c credential.helper=...` (gh token as basic-auth
  password) with the broken gitconfig proxy bypassed: github.com git
  smart-HTTP was reachable, the socks5h proxy and bearer-auth were not.
- **PR: https://github.com/WindWang2/exp-rs/pull/1508** (base=master,
  6 commits, state OPEN). No auto-merge; CI not awaited (per task stop
  conditions). File-overlap with sibling PRs #1506/#1507 verified
  disjoint (only shared file is tests/CMakeLists.txt at disjoint lines).

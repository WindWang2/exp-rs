# EVIDENCE — eo-ai-model-runtime-foundation-10

Policy: every capability claim maps to a local command + exit code, or is
explicitly marked not-executed. No online CI.

## Environment

- Host: 16 cores / 62 GB; policy -j2, drop -j1 on RSS>70% or load>1.5×16.
  Host load measured 9–19 during the build window (concurrent 10.0 tracks);
  -j2 held, no RSS pressure trigger fired.
- ccache 4.14 at /usr/sbin/ccache (8.1G cache) used via
  CMAKE_{C,CXX}_COMPILER_LAUNCHER.

## Commands (chronological)

1. `git fetch --all --prune` → clean; `git pull --ff-only` → "已经是最新的";
   origin/master @ `7d78059d1a` (BASELINE.md).
2. `git worktree add ../exp-rs-eo-ai-model-runtime-foundation-10 -b
   zcode/eo-ai-model-runtime-foundation-10 origin/master` → exit 0.
3. `git check-ignore -v .planning/<slug>/GOAL.md` → matches the NEGATED
   whitelist pattern `!.planning/eo-ai-model-runtime-foundation-10/*.md`
   (file tracked; `git add` succeeded — the 12 planning files are in the
   phase-0 commit `da0fb63696`).
4. `cmake --preset dev-default -DFETCHCONTENT_SOURCE_DIR_PYBIND11=
   /home/kevin/projects/rs-studio/main/build-dev/_deps/pybind11-src
   -DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/ccache
   -DCMAKE_C_COMPILER_LAUNCHER=/usr/bin/ccache` → **exit 0**
   ("Generating done", 10.6s). The pybind11 offline source override follows
   the temporal-track evidence (FETCHCONTENT network fetch fails offline).
5. `cmake --build build-dev --target test_detection_nms_10 test_eo_platform_10 -j2`
   → running in background during code writing (log /tmp/eo10-build1.log;
   exit code recorded below when complete).
6. `cmake --build build-dev --target test_detection_nms_10 test_eo_platform_10 -j2`
   → **exit 0** after fixes (see 10 below). Intermediate failures recorded honestly:
   - Run 1: link failed — the three new provider/preflight TUs were added to
     CMakeLists AFTER the build started (no reconfigure mid-make); rerun fixed.
   - Run 2: `eo_preflight.cpp:48/52` — local Json::Value shadowed struct members
     in EoPreflightReport::toJson; fixed by renaming locals.
   - Run 3: test-TU errors (wrong using-declaration namespace, cv::Mat
     4-Range operator misuse, missing includes, `auto catalog = instance()`
     copying the singleton → incomplete VerifiedArtifact); fixed.
   - Run 4: **link failure "设备上没有空间"** — root filesystem 100% full.
     Host-level cleanup: `ccache -M 3G` + `ccache -c` (trimmed 8.5G → 2.8G);
     freed 5.8G; no track artifacts touched.
7. `QT_QPA_PLATFORM=offscreen ./tests/test_detection_nms_10` → first run 3/4
   failed on equivalence; ROOT CAUSE: the TEST-LOCAL dense reference had a
   typo (`std::max( ay1, bx1 )` in the interH term — an X coordinate in the Y
   overlap). The LIBRARY grid NMS was verified correct against a corrected
   dense reference (offline replay of the dumped field: element-wise
   identical, 1968/1968 kept). After fixing the test typo:
   **All tests passed (12 assertions in 4 test cases)** — includes 100k-box
   bounded completion (<10 s guard; dense would need ~5e9 IoU compares) and
   typed mid-pass cancellation.
8. `QT_QPA_PLATFORM=offscreen ./tests/test_eo_platform_10` → 3 failures
   (F-OPS-2 test premise: dim-0 single-plane ROI is actually continuous —
   fixed with a dim-1 ROI; WP-A eo validation was nested inside the
   detectionDeclared scope in parseManifest — MOVED to parse level, a real
   code fix; rs:regress expectation: identity runtime maps 2 fed channels →
   2 output bands) → after fixes: **All tests passed (570 assertions in 21
   test cases)**.

## Phase 7 review-fix verification (final code state)

- `cmake --build build-dev --target <13 test targets> -j2` → exit 0.
- Final matrix (QT_QPA_PLATFORM=offscreen): test_detection_nms_10 (16/5),
  test_eo_platform_10 (576/21), test_model_runtime (100394/21),
  test_model_runtime_8 (1770/14), test_model_runtime_9 (190/16),
  test_model_tasks (1249/9), test_multimodal_inference (8324/8),
  test_model_failure_matrix (126/16), test_model_catalog_v2 (383/23),
  test_model_manifest7 (62/14), test_model_library_manifests (237/6),
  test_algorithm_meta_drift (2696/1), test_capability_knowledge (1055/12),
  test_toolbox_coverage (108/4), test_spatial_tools (139/16),
  test_agent_tool_catalog (4169/9), test_harness_catalog (109/5) — ALL
  "All tests passed".
- test_capability_drift: 19/21 — remaining 2 failures proven byte-identical
  on a master @ 7d78059d build of the SAME suite (assertions
  `uncovered.empty()` tools-floor at :202/203 and `recipe_id != ...` at
  :753/754) — pre-existing, OUT_OF_SCOPE (harness track).
- NMS perf (Phase-5 evidence, Debug -O0): 100k disjoint dedup 304 ms; 50k
  dense-cluster NMS 105 ms (dense ≈ 5e9 IoU compares at the budget).

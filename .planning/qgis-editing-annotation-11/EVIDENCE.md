# EVIDENCE — qgis-editing-annotation-11

Local evidence only. No online CI is triggered, awaited, or cited. Every claim maps to a command + exit code or is marked not-executed.

## Phase 0 — audit & planning (2026-09-15)

- `git fetch origin --prune` → OK (pruned 5 stale remote branches incl. grok/unified-mission-workbench-d18).
- `git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`.
- `git log -20 --oneline --decorate origin/master` → head: a5b11b7f10 "fix: fail-closed fixes for review issues #994–#999 (#1000)"; #991/#992 merged (c5d4aafe8e, 1cea98921b).
- `gh pr list --state open` → single PR #1008 `zcode/radiometric-spectral-workbench` (CONFLICTING, not draft). Files enumerated into PARALLEL_OWNERSHIP.md.
- `gh issue list --state open` → #1001–#1007 (R2 findings; dedupe table in BASELINE.md).
- `sed -n '1,260p' ISSUES.md` → old D3 operator-gap backlog; not an editing backlog; not implemented from.
- `sed -n '1,260p' CHANGELOG.md`, `sed -n '1,320p' docs/agents/goal-template.md` → conventions read.
- Skills existence check (`ls .agents/skills/<name>/`): codebase-design ✓, code-review ✓, diagnosing-bugs ✓, domain-modeling ✓, ask-matt ✓, implement-spec ✓, implement ✓, resolving-merge-conflicts ✓.
- Worktree: `git worktree add ../exp-rs-qgis-editing-annotation-11 -b zcode/qgis-editing-annotation-11 origin/master` → OK.
- `.gitignore` whitelist added (D18/D19 two-line style) → verified: `git check-ignore -v .planning/qgis-editing-annotation-11/GOAL.md` → no output (exit 1, not ignored).
- Subagent #1 (read-only Explore) audit: test infra (Catch2, sicnu_add_test, ensureApp/QgisFixture patterns, offscreen properties), agent tool surface (SpatialTool/Registry/Provider prefixes, WorkbenchContextTool mount precedent), planning conventions (.gitignore styles, ADR next=0163, d19 file set), build (preset `dev-default`, generator Unix Makefiles, build-dev cache exists), editing state (MapToolManager toolset, main_window_vector flows, vertextool port), gui maptools full-impl (no stubs), annotations compiled w/ zero app consumers, docs/workbench absent. Facts merged into BASELINE/CURRENT_ARCHITECTURE.

## Phase 1 — EditSession authority

- Files: `src/app/editing/rs_edit_session.{h,cpp}`, `rs_edit_command_guard.h`, `tests/test_edit_session.cpp`.
- Configure (fresh build-dev, offline box): `cmake --preset dev-default -DFETCHCONTENT_SOURCE_DIR_CATCH2=/home/kevin/projects/rs-studio/main/build-dev/_deps/catch2-src` → exit 0 (network clone of Catch2 fails: TLS unexpected EOF; reused already-populated dep source).
- Build: `cmake --build build-dev --target test_edit_session sicnu_geo_rs -j2` (long: full qgis_core/gui/analysis compile at -j2 under concurrent-track load ~15-21; logs /tmp/build_f11*.log).
- Test: `ctest -R "^test_edit_session$" -j1` → **15 cases / 120 assertions, all passed** (first green 2026-09-16 ~01:43).

## Phase 2 — snapping/validity + brush/erase/annotation

- Files: `rs_snapping_controller.*`, `rs_geometry_validity.*`, `rs_sample_brush_tool.*`, `rs_sample_erase_tool.*`, `rs_annotation_controller.*` + `tests/test_edit_{snapping,validity,sample_tools,annotation}.cpp`.
- Compile fixes discovered by the build: QPointer members need complete types in headers; `QgsGeometry::combine` is an instance method here (fold instead of static union); `QgsPointLocator::Match::type()` returns the legacy local enum; canvas events are protected in base (declared public in our tools, mirroring `rs_roi_tool_*`).
- Test-drive contract: event positions are derived from map coordinates via `QgsMapToPixel::transform` (this fork's map→screen op) — immune to y-flip and device pixel ratio; erase test data corrected to map (100,100)→(300,300).

## Phase 3 — ROI semantics + large-editing

- Files: `rs_roi_semantics.*`, `rs_edit_index.*` + `tests/test_edit_{roi_semantics,index}.cpp`; `qgis_analysis` added to `sicnu_geo_rs` link (one line) — `RsPixelRasterizer` is the only membership oracle.
- Boundary honesty: GDAL's inside test is boundary-sensitive; the known-answer ROI encloses (never touches) pixel centers — 42 px for cols {4..9} × rows {2..8}. makeValid of an INVALID bow-tie has a meaningless signed input area (0 by shoelace); the repaired hand-truth is 50 m².

## Phase 4 — agent surface + persistence + integration

- Files: `rs_edit_agent_tool.*`, `rs_edit_persistence.*` + `tests/test_edit_{agent_state,persistence}.cpp`; `spatial_tool_provider.cpp` (+4/-1: `editing:` prefix → group `editing`); `main_window_workbench.cpp` (+19: session+snapping+`editing:state` mount next to `WorkbenchContextTool`).
- Persistence: `QgsVectorFileWriter` GeoJSON driver REWRITES the requested filename — atomic rename now follows the writer's `newFilename` out-param, verified by the temp-residue/failure-preservation tests.
- Contract fix found by tests: brush strokes are MULTIPART — single-part target layers are refused explicitly (previously such layers only failed at commit: "geometry type is not compatible"). Docs updated.
- Integration compile evidence: `main_window_workbench.cpp` (with the F11 mount) compiled standalone with the app's exact flags → TU_EXIT=0, 0 errors (app TARGET link blocked by the pre-existing workflow break, see OUT_OF_SCOPE).

## Phase 5 — hardening

- Failure matrix additions pinned by tests: commit rejection with empty error list (setAllowCommit) → explicit fallback message; writer success without file → explicit error; invalid raster CRS → refused; unsupported export suffix → refused pre-IO; nearestNeighbor k-boundary extra entry tolerated in oracle; empty seed lists don't pollute undo depth.
- Resource: stroke cap 2048 stamps with coalescing; index attach cap 100k (env `RS_EDIT_INDEX_MAX_FEATURES` verified via qputenv round-trip); ROI preview budget 4M px fail-closed (verified refusal path with 10 px budget).

## Phase 6 — E2E + docs

- `tests/test_editing_e2e.cpp`: full composed chain (canvas+session+brush→validity→ROI stats→label→undo/redo→commit→GeoJSON export with class=3→layer removal mid-session→project clear→agent facts after lifecycle) → passed.
- Docs: `docs/adr/0163-editing-session-authority.md`, `docs/workbench/editing-platform.md`, CHANGELOG section.

### Targeted suite results (first full-green run, 2026-09-16)

`ctest --test-dir build-dev -R "test_edit" -j1` (env `QT_QPA_PLATFORM=offscreen`) → **100% tests passed, 10/10**:
test_edit_session, test_edit_snapping, test_edit_validity, test_edit_sample_tools, test_edit_annotation, test_edit_roi_semantics, test_edit_index, test_edit_agent_state, test_edit_persistence, test_editing_e2e.

## Phase 7 — review

(filled)

## Phase 8 — final verification + PR (2026-09-16)

- Rebase: `git fetch origin` hit transient TLS failures on github HTTPS; local `origin/master`
  unchanged at the baseline `a5b11b7f10`, so `git rebase origin/master` → "up to date" (branch is a
  linear child of the baseline).
- **Oracle O6 double-run (post-remediation, final code):**
  - RUN 1: `ctest -R "test_edit" -j1` (offscreen) → 100% tests passed, 10/10, 9.34 s
  - RUN 2: same command → 100% tests passed, 10/10, 9.28 s
- Hygiene: `git diff --check origin/master...HEAD` → clean (exit 0); conflict-marker scan → 0 files;
  planning whitelist confirmed (`git check-ignore` → not ignored).
- Review remediation batch re-verified by the double-run above (REVIEW_LOG Round 1: P0=0,
  P1 2/2 fixed, P2 3 fixed + 1 documented, P3 6 fixed + 2 dispositioned).
- App-target status (honest): `sicnu_geo_rs` link is blocked on the baseline by the pre-existing
  `WorkflowDefinition` redefinition (see OUT_OF_SCOPE; pristine master TUs fail identically). This
  track's integration TU (`main_window_workbench.cpp` with the F11 mount) compiles standalone with
  the app's exact flags (TU_EXIT=0, 0 errors); every `test_edit_*` target builds and passes.
- PR created from `.planning/qgis-editing-annotation-11/PR_BODY.md`; not merged; no CI awaited.
- Push/PR network note: github git/api endpoints dropped TLS handshakes for ~1 h (curl to the bare
  host still worked; EOF on push/ls-remote/api). A detached retry loop pushed successfully on
  recovery at 06:54:56 and `gh pr create` completed → **https://github.com/WindWang2/exp-rs/pull/1013**.
  Full log: /tmp/f11_push.log (also mirrored in the ledger). Force-push was never used; the branch
  landed as a linear child of the baseline.

## OUT_OF_SCOPE

- Issues #1001–#1007 (io/workflow/dataset/agent-sample/georef domains) — dispositions in BASELINE.md; not touched.
- PR #1008 business files — untouched.
- Legacy `main_window_vector.cpp` dialog-flow migration onto the session — follow-up.
- Vendored QGIS cleanups — none.
- **P0 (out of scope, pre-existing on master a5b11b7f10): `sicnu_geo_rs` app target does not compile.**
  `src/workflow/workflow_ir_v2.h:97` and `src/workflow/workflow_types.h:47` both define
  `struct sicnu::workflow::WorkflowDefinition` → any TU whose include chain sees both fails
  (redefinition). Verified NOT caused by this diff: `g++` of the pristine `origin/master`
  `main_window_view.cpp` / `main_window_workbench.cpp` with this build's exact flags fails with the
  identical errors (/tmp/mwv_err.log, /tmp/mww_orig_err.log). The main repo's build-dev predates the
  D17/D19 merges (Aug 29), so this has never been compiled locally. Repair = renaming/reshaping a
  workflow-domain type across 14+ files — explicitly out of scope for this track; recorded here and
  at the top of PR_BODY.md. Two SMALLER unblocking breaks in the same class WERE fixed in this diff
  (1-line each, see commit `fix(agent/workbench)`): missing `using sicnu::experiment::*` in
  `data_platform_tools.cpp` and missing `georef_dual_window.h` include in `main_window_workbench.cpp`.
- Not-executed: 60 s-interval CPU/RSS sampling during long builds (detached nohup process; sampled
  load averages at poll points instead: 15.5–21 on 16 cores, memory ≤19%; hard cap -j2 held).

## Budget ledger (phase → tool calls / files touched / clock)

(filled per phase)

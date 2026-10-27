# BASELINE — R24-C (Pi/MCP Harness × Qt/QGIS Workbench × Cartography Interaction)

- UTC time of recon: 2026-10-27T02:24–02:45Z
- Baseline `origin/master`: `5fe642ec6339807823aa56612ba94668592a738d`
  (= reference snapshot in the goal; local master was behind at `ad98a7a70`, not checked out for writes)
- Track worktree: `../exp-rs-r24_c-20261027T022556Z`, branch `agent/r24_c-20261027T022556Z` (fresh from origin/master, clean status)
- Main checkout `C:/Users/wangj.KEVIN/projects/exp-rs` left untouched (one untracked file `data/samples/manifest.json` preserved read-only).
- Sibling worktree present: `exp-rs-D-20261026T165151Z` (r23-D, PR #1717 MERGED) — avoided, not modified.

## Repository state
- Open PRs: **0**. Latest merged: #1717 (r23-D lab grading evidence, merge `5fe642ec6`), #1716 (r23-E plugin/worker/delivery regression guards, merge `cab63a2c2`).
- Remote branches: `origin/master` + two merged r23 residual branches (`agent/r23-d-…`, `agent/r23-e-…`).
- Open issues: ~140, overwhelmingly stale (fixes already merged; GitHub issues not closed by the fixing PRs).

## Environment (measured, not assumed)
- Windows 10.0.26200 x64, Git Bash shell; 1 socket / 16 logical CPUs; RAM 32,707,444 KiB total, ~9.5 GiB free at recon → **build -j2 default** (cap 4 per goal only when free RAM ≥12 GiB and no other builders).
- Disk: C: 660 GiB free.
- Toolchain (from main build-dev/CMakeCache + scripts/windows/_env.cmd probe): MSVC 14.38.33130 (VS2022 Community), Ninja, Qt 6.8.0 msvc2022_64 at `C:\deps\Qt\6.8.0\msvc2022_64`, vcpkg toolchain `C:/deps/vcpkg/scripts/buildsystems/vcpkg.cmake`, CMAKE_PREFIX_PATH also `C:\deps\qca-install;C:\deps\kc-install`. vcpkg binary cache ~2.9 GiB (warm-ish). cmake binary at VS-bundled path (not on bash PATH).
- Build dir for this track: `build-r24_c` (RelWithDebInfo, ENABLE_TESTS=ON, Ninja, CMAKE_BUILD_PARALLEL_LEVEL=2).
- Qt tests: `QT_QPA_PLATFORM=offscreen`, `CTEST_PARALLEL_LEVEL=1`.

## Real code map (read, not README-derived; full detail in explorer reports summarized here)
- `src/agent/` ~89.6k LOC, ~200 files. MCP server `src/agent/mcp_server.cpp` (3186 ln): `--mcp` startup `src/app/main.cpp:267-331`, StdinReader hardened (clean-EOF/badbit/#1693 dead branch removed), 4 MiB line cap, `notifications/cancelled` with rpc-id keyed maps + poller blacklist, Surface-11 progress relay, workspace containment via `tool_catalog/workspace_containment.h` + `isFreeTextSlot` prose exemption (mcp_server.cpp:172-207). Tool surface: `collectSurfaceTools()` union projection; ~130 spatial registry tools + meta protocol tools.
- `pi/` 5.7k LOC: `exp-rs-spatial.ts` (tool registration shell + `exprs_wait_for_execution`/`exprs_status`), `mcp_bridge.ts` (transport: spawn `--mcp`, initialize handshake 30s, circuit breaker, desync detection, 4 MiB outbound cap, 10-min per-request timeout w/ server-side cancel, teardown ladder). 9 node --test files incl. fake MCP server.
- `src/app/` ~137k LOC: `QgisDesktopWindow` partial-class shell; CommandRegistry + SelectionContext + WorkbenchHost infra (`src/app/workbench/`); TaskCenter GUI projections `rs_job_panel`, `task_panel_host`, `processing_history_panel`; edit lease via `SicnuEditLeaseCoordinator` (unified by #1677); cartography dock submits the SAME `cartography:*` RSOperators as headless; layout designer `src/app/layout/qgslayoutdesignerdialog.cpp`.
- `src/workflow/` ~16k LOC: WorkflowRuntime (sessions) + WorkflowRunCoordinator (production seam, checkpoints atomic tmp+fsync+rename, resume w/ artifact verification, explain diagnostics); repair engine closed-form rule table (CrsMismatch→gdal:reproject, ResolutionMismatch→rs:resample, RadiometricStateMismatch→calibration chain; **DataTypeMismatch/DimensionMismatch have no rule**).
- `src/help/` + `src/app/help/`: HelpRegistry/composition/F1 filter; zh_CN .ts ~23,486 entries, 5 unfinished.

## Do-not-redo list (merged PR scopes verified via git)
- #1714 (r22-agent): MCP sandbox driver-prefix escape, env-key splice injection, ghost py: algorithms, IPC >1MiB hang, JobEngine bounded logs. Files: mcp_server.cpp, workspace_containment.h, src/jobs, src/python, src/sdk.
- #1713 (r22-workbench): 9 workbench usability fixes (async runner lifecycle, temporal page routing, zoom-native CRS, group-remove lease, log tag filter, toolbox close-once, map-tool inventory, ImportCenter retire, EN punctuation).
- #1677 (r21-D): unified edit lease + cartography/MapSpec/export quality (frame CRS into map item, atlas filter refusal, export cancel seam, delivery set = written set).
- #1675 (R21-C): Pi/MCP fail-closed (poller-blacklist cancel gate, terminal-session journaling, poll_interval clamp, CLI flags), pi/test/r21_c_poll_interval_clamp.test.mjs.
- #1716 (r23-E): regression-guard-only PR, **zero src/ changes**, added `scripts/check_merge_registration_integrity.py`.
- #1717 (r23-D): lab grading authority `ndvi_landsat` + lab runtime fail-closed; src/lab only.

## Cross-track inputs read
- `.planning/zcode-r23-E/CROSS_TRACK_FINDINGS.md`: #1699 residual wall-clock assertions — 8 files assigned to C track (table reproduced in ISSUE_TRIAGE).
- `.planning/zcode-r23-D/CROSS_TRACK_FINDINGS.md`: CT-D1 (run_lab.cmd still points `--lab ndvi_basics` → 45/100; suggested one-line E-track fix `--lab ndvi_landsat`), CT-D2 (curriculum_editor readBounded fixed — no action), CT-D3 (dual verifier: production authority = src/verify, no action).

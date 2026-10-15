# R14-C Baseline (Pi/MCP Agent Harness, Tool Calling & Workflow Execution)

- **Date:** 2026-10-15
- **BASE_SHA:** `f72f0c11731361488a384a085797aae64efaf7ee` (origin/master, verified by `git rev-parse` after `git fetch --prune`)
- **Worktree:** `../exp-rs-r14-C-pi-mcp-worktree` (branch `hardening/r14-C-pi-mcp-workflow`, created from origin/master with git-meta flock)
- **Host note:** Executed in ZCode (not Google Antigravity). `/teamwork-preview` and `/goal-loop` are not registered skills in this host; per prompt §1.1 fallback, the same analysis/implement/verify/independent-review pipeline runs via native subagents (Agent tool). No slash-command activation is claimed.

## Live repository state (2026-10-15, gh CLI)

- Open PRs: **#1504** (`hardening/r12-b-geodata-durability` — geospatial/data/dataset/experiment), **#1505** (`hardening/r13-j-security-concurrency-fixes` — touches `src/python/isolated/python_worker_process_pool.*`, `python_plugin_adapter.cpp`, geospatial/remote, core/temporal_cube, runtime/gpu, science_context/broker).
  - **#1505 collision rule for this track:** `src/python/isolated/python_worker_process_pool.{cpp,h}` and `python_plugin_adapter.cpp` are read-only for C; verify-and-dedupe only.
- Open issues: 31 (list captured in `ISSUE_REVIEW_MATRIX.md`).
- Remote branches: 11 stale hardening branches (r12/r13) — merged but undeleted.
- Recently merged into baseline: #1483, #1484 (agent/workflow runtime), #1496, #1497, #1498, #1500 (scientific planning — planner/repair_planner), #1501 (claims 29 issues fixed), #1502, #1503.

## Environment facts

- git fetch requires bypassing a dead global socks5 proxy for github.com (`git -c http.https://github.com/.proxy= fetch`); gh CLI authenticated as WindWang2.
- Build: Ninja, system Qt6 (`/usr/lib/cmake/Qt6`), GDAL from `/home/kevin/pwb-sdks/root/usr`, `SICNU_VENDOR_GDAL=OFF`, Debug.
- Catch2 FetchContent must be pointed at the existing local source (`-DFETCHCONTENT_SOURCE_DIR_CATCH2=/home/kevin/project/exp-rs/build-review/_deps/catch2-src`) — network clone fails through the dead proxy.
- `scripts/build.sh` is not executable in this checkout → invoke via `bash scripts/build.sh ...`.
- Resource policy: builds `-j2` under `/tmp/exp-rs-r14-global-build.lock`, ctest `-j1`, `QT_QPA_PLATFORM=offscreen` (per CLAUDE.md / scripts/build.sh / resource_guard.py).

## Build tree

- `build-r14-C/` configured via `bash scripts/build.sh configure --base-cache ../exp-rs/build-review/CMakeCache.txt -- -DFETCHCONTENT_SOURCE_DIR_CATCH2=<local>` — exit 0.

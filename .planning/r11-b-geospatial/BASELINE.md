# BASELINE & REMOTE RECONNAISSANCE — Direction B (Geospatial Data Fabric & Persistence)

**Date**: 2026-10-13  
**Repository**: `https://github.com/WindWang2/exp-rs`  
**Track**: Direction B (`hardening/r11-b-geospatial-data`)  
**Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r11-b-geospatial`  

---

## 1. Remote and Baseline Commit State

### `git remote -v`
```
itk-upstream	https://github.com/InsightSoftwareConsortium/ITK.git (fetch)
itk-upstream	https://github.com/InsightSoftwareConsortium/ITK.git (push)
origin	https://github.com/WindWang2/exp-rs.git (fetch)
origin	https://github.com/WindWang2/exp-rs.git (push)
```

### `git status --porcelain=v1 -b` (Main Worktree at creation)
```
## master...origin/master
```
Status: Clean.

### `git fetch origin --prune`
Executed cleanly.

### `git rev-parse origin/master`
`8a08642760e2e8ffd2c4b965fc4683c01d3e4758`

### `git log -15 --date=iso --format='%H %ad %s' origin/master`
```
8a08642760e2e8ffd2c4b965fc4683c01d3e4758 2026-10-12 02:47:51 +0800 Merge hardening/r6-digest-identity-convergence: MSVC configure unblock (WHOLE_ARCHIVE twin-item conflict)
09a7ef5bd6c6904bd75df39329c3058ef275f40b 2026-10-12 02:46:56 +0800 Merge rs14-unified-verifier: unified scientific verifier subsystem (RS14, ADR branch doc)
84d782c408ab6de5ede088a5015de76ce33e41d9 2026-10-12 02:35:01 +0800 Merge pull request #1474 from WindWang2/hardening/r8-ui-teaching-context
78148cc006c6dc893fc242d5cfb956c64be6372b 2026-10-12 02:34:51 +0800 Merge origin/master (PRs #1469-#1473) into hardening/r8-ui-teaching-context
cec4dc44913df881aa5a17fa770403c30f4eb462 2026-10-12 02:33:57 +0800 Merge pull request #1473 from WindWang2/hardening/r8-data-experiment-integrity
f0e44192a6b366575f959fd04bceab98f4e4dac1 2026-10-12 02:33:48 +0800 Merge pull request #1472 from WindWang2/hardening/r8-science-algorithm-closure
57816eebb997f0758c58f339eefce490672fd96c 2026-10-12 02:33:29 +0800 Merge origin/master (PR #1469) into hardening/r8-science-algorithm-closure
107e2b17d73df6d6f161a3938c28d512e01b38af 2026-10-12 02:30:11 +0800 Merge pull request #1471 from WindWang2/hardening/r8-runtime-boundaries
741bd54227f60292fe2778861c9fca38e7dbecca 2026-10-12 02:30:02 +0800 Merge origin/master (PR #1469) into hardening/r8-runtime-boundaries
87806e90e0381b22ba5320fe8b694165566036dd 2026-10-12 02:29:06 +0800 Merge pull request #1470 from WindWang2/hardening/r8-agentops-workflow-closure
35e644c31d78f5d9781659ad296e68caad9000e7 2026-10-12 02:28:54 +0800 Merge origin/master (PR #1469) into hardening/r8-agentops-workflow-closure
428dcef4eaaf661ce4e521bcbf56d4d95231c1b8 2026-10-12 02:24:45 +0800 Merge pull request #1469 from WindWang2/fix/review-p0-p1-wave
569ce67293f23216a0d653c470fe672b26dacf09 2026-10-12 01:41:36 +0800 fix(tests): final adversarial-review round — close the three tip-red paths
3112cfb94ca1f4336e3b23f32e0c8aa04a3d31e3 2026-10-12 01:23:14 +0800 chore(app): remove rs_job_runner — dead code with a latent callback race (#1453)
b03cd066bb41ea4d9ef3881aeaf99a93db99930d 2026-10-12 01:23:04 +0800 fix(app): UI state honesty — workflow gating, fail-closed affordances, registry menu projection; + adversarial-review fixes (#1453/WP02/WP06)
```

---

## 2. GitHub CLI / API Reconnaissance

### `gh pr list --repo WindWang2/exp-rs --state open --limit 100`
- Result: `HTTP 401: Requires authentication (https://api.github.com/graphql)`.
- Status: `UNAVAILABLE` via GitHub CLI auth; recovered via tracking branches in git remote.

### `gh issue list --repo WindWang2/exp-rs --state open --limit 200`
- Result: `HTTP 401: Requires authentication (https://api.github.com/graphql)`.
- Status: `UNAVAILABLE` via GitHub CLI auth; cross-referenced via git commit logs and issue references in PRs (#1456, #1457, #1448, #1450, #1467, #1472, #1473).

---

## 3. Remote Tracking Branches for Open PRs

| PR Number | Remote Branch Name | Area | Key Files Touched |
|---|---|---|---|
| #1475 | `origin/hardening/r9-science-data-integrity` | Science & Data Integrity | `src/core/qgsdatasourceresolver.cpp`, `src/core/radiometric_state.*`, `src/data/data_manager.cpp`, `src/data/workspace_catalog.cpp`, `src/dataset/dataset_store_*`, `src/geospatial/fabric/query_planner.cpp`, `src/operators/framework/rs_json_params.cpp`, `src/operators/framework/model_catalog.*`, `src/suitability/*` |
| #1476 | `origin/hardening/r9-ui-teaching-test-quality` | UI & Teaching Quality | `src/app/widgets/guided_workflow_widget.cpp`, `src/app/widgets/rs_scan_pool.h`, `src/faultlab/fault_transforms.cpp`, `src/teaching_admin/script_adapters.*` |
| #1477 | `origin/hardening/r9-agent-execution-reliability` | Agent & Runtime Execution | `src/operators/runtime/model_runtime.*`, `src/processing/framework/task_center.*`, `src/python/isolated/*`, `src/sdk/exprs/*` |
| #1478 | `origin/hardening/r10-ui-teaching-help-consistency` | UI & Teaching Consistency | `src/app/experiment_studio/*`, `src/app/shell/workflow_session_controller.cpp`, `src/app/workbench/command_defs.cpp`, `src/app/workflow/pipeline_editor_dock.*`, `data/help/workbenches.json` |

---

## 4. Concurrent Active Worktrees on Host

- `/home/kevin/projects/rs-studio/exp-rs-r11-a-science` (Direction A: Algorithms & Numerical Contracts)
- `/home/kevin/projects/rs-studio/exp-rs-r11-c-agent` (Direction C: Agent Execution Plane & Runtime)
- `/home/kevin/projects/rs-studio/exp-rs-r11-b-geospatial` (Direction B: **THIS INSTANCE**)

**Concurrency Control**:
Strict build leasing via flock / serialization to guarantee host parallel compile load does not exceed `-j2`.

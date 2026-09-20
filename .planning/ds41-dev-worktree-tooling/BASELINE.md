# BASELINE — ds41-dev-worktree-tooling (Track D5)

All facts below were re-verified on 2026-09-20 (~00:30–00:50 local) against the live
repository; the brief's snapshot values are marked as stale where they differ.

## Live master

| Fact | Evidence |
|---|---|
| `origin/master` = `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` | `git fetch origin --prune` (rc=0) then `git rev-parse origin/master` |
| Brief snapshot `2761a6857…` is 2 commits behind live master | live log: `adf8f9895 docs(agents): record Platform 5.0 audit request`, `fe7da0622 feat(agent): add StepFun preset provider profile` |
| Worktree base = the freshly fetched `origin/master` | `git worktree add … -b agent/ds41-dev-worktree-tooling origin/master` → `git rev-parse HEAD` = `adf8f9895…` |
| Branch `agent/ds41-dev-worktree-tooling` did not exist before this track | `git branch -r` / `git branch --list` showed no such ref; worktree list had 20 entries, none named for this track |

## GitHub state (via `gh`, local credential store)

| Fact | Evidence |
|---|---|
| Open PRs = 0 | `gh pr list --state open --limit 100` → empty, rc=0 |
| Open issues = 0 | `gh issue list --state open --limit 200` → empty, rc=0 |
| Most recent merged PRs | `gh pr list --state merged --limit 80`: #1115, #1113, #1112, #1111, #1110, #1109, #1108, #1107, #1106, #1105, #1104, #1103, #1102, #1101, #1100, #1098, #1070 … |
| All closed issues up to #1097 are CLOSED | `gh issue list --state closed --limit 100` — latest closed is #1097 (2026-09-19) |
| No open PR/issue covers agent-dev worktree tooling | No open items exist at all; merged list contains no `scripts/dev` or `docs/development` work |

## Remote historical branches (13)

`git branch -r` shows 13 `agent/*` / `fix/*` branches besides master. Ahead/behind vs live master
(`git rev-list --count`) and classification (`git cherry` patch-id + content spot checks):

| Branch | ahead/behind | Verdict |
|---|---|---|
| `agent/ds41-http-fetch-strict` | 2 / 80 | Source fix #1034 superseded by #1110 (master `http_fetch.cpp` no longer has the stray `if (fetch.httpStatus == 404)` guard; see `src/geospatial/remote/http_fetch.cpp:280`). **Unmerged increment: its regression test `tests/test_io_http_fetch.cpp` (207 lines) plus `tests/CMakeLists.txt` registration is not in master** — an asset the squash dropped. |
| `agent/ds41-pipeline-drag-lifetime` | 4 / 80 | Superseded by #1101 (PipelineScene drag lifetime, issue #1049). |
| `agent/flash-data-transaction-integrity` | 4 / 80 | Superseded by #1105 (data transactions, issues #1045/#1095 family). |
| `agent/flash-geo-fabric-integrity` | 4 / 80 | Superseded by #1100 (geospatial fail-closed, #1053/#1054). |
| `agent/flash-lab-foundry-determinism` | 6 / 80 | Superseded by #1107 (foundry determinism, #1096/#1087 family). |
| `agent/flash-mcp-containment-routing` | 5 / 80 | Superseded by #1106 (MCP containment, #1033). |
| `agent/flash-processing-atomic-errors` | 5 / 80 | Superseded by #1104 (processing error paths, #1043/#1090/#1091). |
| `agent/flash-workflow-integrity` | 8 / 80 | Superseded by #1107/#1113 (workflow resume/cancel/lineage, #1077/#1078). |
| `agent/glm53-desktop-lifecycle` | 12 / 80 | Superseded by #1101/#1102 (app map-tool/rubber-band lifetime). |
| `agent/glm53-plugin-sdk-trust` | 7 / 80 | Superseded by #1103 (plugin SDK trust, #1036/#1039/#1040). |
| `fix/ci-master-unblock` | 2 / 80 | Superseded by #1108 (master carries `find_package(Protobuf CONFIG)`-with-`MODULE` fallback, `CMakeLists.txt:129-140`, and `CPLErrorHandlerPusher` quiet guards in `src/geospatial/io/stage_ledger.cpp:291`). |
| `fix/r2-ci-protobuf-multimode` | 1 / 80 | Superseded by #1108 (same CMake change). |
| `fix/review-issues-1033-1056` | 1 / 80 | Superseded by the r2-* PR wave (#1100–#1106) + #1115; content landed piecewise. |

None is a valid development baseline (all ~80 commits behind). One carries an unmerged test
asset — reported by WP6 as a recommendation, never auto-actioned.

## Local environment facts (tool design inputs)

| Fact | Evidence |
|---|---|
| 20 agent worktrees exist under `../exp-rs-worktrees/` | `ls ../exp-rs-worktrees` (ci-unblock, ds41-build-portability, glm53-*, r2-*, …) |
| `scripts/dev/**` and `docs/development/**` do not exist yet | `ls scripts/dev` / `ls docs/development` → no such directory |
| `.planning/*` is gitignored with a per-track `*.md` whitelist | `.gitignore:127` + whitelist lines; this track appends its own block (runbook step 2) |
| No pre-existing preflight/worktree/lock tooling in `scripts/` | grep for "worktree" in `scripts/**` finds only doc mentions, no tool |
| Python 3.13.9 available (Anaconda), `unittest` in stdlib | `python --version` → `Python 3.13.9`; `import unittest` ok |
| Concurrent agents fetch this repo from the same clone — remote-tracking refs can be momentarily unresolvable from another process | observed 2026-09-20 00:35–00:38: five `fetch origin --prune` entries in `.git/logs/refs/remotes/origin/master` from overlapping timestamps; one sandboxed read of `origin/agent/*` returned "unknown revision" while the same refs resolved seconds later. Design input: retry read-only git queries. |
| Sandboxed shell cannot reach `github.com:443`; unsandboxed can, and `api.github.com` is reachable from both | `curl https://github.com` times out in-sandbox; `git ls-remote`/`git fetch` succeed unsandboxed; `gh pr list` works in-sandbox. Design input: tools must degrade, not crash, when the network is unavailable. |

## Census of the track's directories (Phase 0 step 4)

| Area | Existing capability | Real gap |
|---|---|---|
| `.agents/` | AGENTS.md conventions (Karpathy 4 rules), orchestrator/reviewer/explorer subagent workspaces, `skills/` mirror (37 skills) | No *dev-workflow* guide tying goal-loop/code-review/worktree conventions to commands |
| `docs/agents/` | `goal-template.md`, `loop-template.md`, `command-vocabulary.md`, triage/domain docs — the prompt-side spec for tracks | No implementation-side counterpart: tools that enforce those rules mechanically |
| `scripts/` | Build/bundle/lab/perf/verification scripts (Python + shell + cmd), `scripts/windows/` helpers | No preflight, no overlap scan, no worktree creator, no build lock, no review-pack generator, no stale-branch reporter |
| `.planning/` | 60+ track planning dirs, whitelist pattern in `.gitignore` | Tooling to create/maintain track scaffolding does not exist (manual per track) |
| Worktrees | `git worktree` used manually by every track | No fail-closed creator; duplicate branch/path and dirty-tree failures are discovered late, manually |
| Builds | `CMakePresets.json`, `cmake --build -j2` conventions, root `build_*.cmd` carry stale paths (defect D-028) | No lock: two agents writing one build dir corrupt each other's builds |

No existing system is re-implemented: the tools orchestrate `git`/`gh`/`cmake` CLIs and add no
new product capability.

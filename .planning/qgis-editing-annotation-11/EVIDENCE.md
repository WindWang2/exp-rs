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

(filled below as work completes)

## Phase 2 — snapping/validity + brush/erase/annotation

(filled)

## Phase 3 — ROI semantics + large-editing

(filled)

## Phase 4 — agent surface + persistence + integration

(filled)

## Phase 5 — hardening

(filled)

## Phase 6 — E2E + docs

(filled)

## Phase 7 — review

(filled)

## Phase 8 — final verification + PR

(filled)

## OUT_OF_SCOPE

- Issues #1001–#1007 (io/workflow/dataset/agent-sample/georef domains) — dispositions in BASELINE.md; not touched.
- PR #1008 business files — untouched.
- Legacy `main_window_vector.cpp` dialog-flow migration onto the session — follow-up.
- Vendored QGIS cleanups — none.

## Budget ledger (phase → tool calls / files touched / clock)

(filled per phase)

# DEDUP — ds41-dev-worktree-tooling (Track D5)

Live-state dedup, re-verified 2026-09-20 after the initial pass. Method: `gh pr list` (all
states), `gh issue list`, `git branch -r`, `git cherry`/patch-id comparison, content spot
checks against master, and inspection of the sibling worktrees' uncommitted state.

## Open work

| Query | Result |
|---|---|
| `gh pr list --state open` (rc=0) | 0 open PRs — nothing to overlap with |
| `gh issue list --state open` | 0 open issues — no ticket claims this scope |
| Nearest merged PRs | #1108 (CI unblock), #1110–#1115 (fail-closed wave) — product fixes, no dev tooling |
| `review/DEDUPE.md` | Covers C++/doc defect dedup for the line-review dossier; unrelated to dev orchestration tooling |

## Sibling tracks with adjacent scope (checked in their worktrees, read-only)

| Track | Scope | Overlap verdict |
|---|---|---|
| `ds41-build-portability` (worktree `../exp-rs-worktrees/ds41-build-portability`, branch `agent/ds41-build-portability` @ `adf8f9895`, planning-only so far) | Cross-platform build, dependency doctor, local gate — CMake/deps | **Adjacent, not overlapping.** It owns CMake/build input portability; this track owns the *wrapper* around build commands (locking + parallelism clamp) and never edits `CMakeLists.txt`. Its `OWNERSHIP.md`/`DEDUP.md` were read to confirm the boundary. |
| `ds41-fuzz-boundaries`, `ci-unblock`, `r2-*`, `glm53-*` worktrees | Product-surface tracks | No file overlap with `scripts/dev/**`, `docs/development/**`, or this `.planning` dir |
| Local `agent/ds41-range-cache-msvc` etc. branches | Product fixes | No overlap |

### `.gitignore` integration hotspot

`ds41-build-portability` has already appended its whitelist block after the
`ds41-range-cache-msvc` block (its working-tree diff, read-only). This track appends its own
block at the same anchor. Both are pure appends of the identical 3-line pattern; if the other
PR merges first, the rebase resolves by keeping both blocks (no semantic conflict). Recorded
here and in the PR body's conflict-hotspot section.

## Historical remote branches

All 13 remote `agent/*` / `fix/*` branches were classified (full table in `BASELINE.md`):
every one is either superseded by a merged PR or a historical residue. None is used as a
development baseline and nothing is cherry-picked.

**One unmerged increment found:** `agent/ds41-http-fetch-strict` carries
`tests/test_io_http_fetch.cpp` (207 lines, regression suite for strict HTTP status semantics,
issue #1034) plus its `tests/CMakeLists.txt` registration. The corresponding source fix
landed via PR #1110, but the test asset was dropped in that squash. Verdict:

- **In scope for this track? No** — it is a product test in `tests/**`, which this track does
  not own.
- **Action:** reported by WP6 (Stale Branch Report) as an explicit recommendation with exact
  paths and `gh pr view`/`git show` evidence, and flagged in this PR's "Known limitations /
  handoff" section, so the owner track (http_fetch) can port the test deliberately. The tooling
  never copies it automatically.

## Prompt-side conventions (read, not duplicated)

`docs/agents/goal-template.md` and `docs/agents/command-vocabulary.md` define the track
prompt spec and are the authority for branch/worktree/planning naming. This track's tools
*implement* those rules (fail-closed creation, planning-scaffold conventions) and the new
`docs/development/` guide *points at* them; neither restates or edits them.

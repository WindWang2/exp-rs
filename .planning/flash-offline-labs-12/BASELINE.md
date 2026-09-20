# BASELINE — flash-offline-labs-12

Snapshot created: 2026-09-20 (UTC, at track start).

## Real-time master state

- `origin/master` at track start: **`adf8f98952422fe9c386c56d64d5fb6a4a6642f1`**
  - `adf8f98952` docs(agents): record Platform 5.0 audit request
  - `fe7da0622c` feat(agent): add StepFun preset provider profile
  - `2761a6857f` Merge PR #1115 (fix: deep-review wave-2 investigation targets) — the SHA named in the track prompt; master moved +2 commits since.
- Worktree: `/home/kevin/projects/rs-studio/exp-rs-worktrees/flash-offline-labs-12`, branch `agent/flash-offline-labs-12`, HEAD = `adf8f98952`.
- Main checkout `/home/kevin/projects/rs-studio/main` has unrelated uncommitted edits
  (`src/cli/cli_commands.cpp`, `tests/test_env_doctor.cpp`, `tests/test_mcp_server.cpp`) — WIP by
  another session; this track does not touch or revert them.

## PR / issue state at pre-read (2026-09-20)

- Open PRs: **0**. Open issues: **0**.
- Recently merged (most relevant to this track):
  - #1032 — land lab-sample-foundry (ADR 0164): the foundry itself.
  - #1107 — confine IR2 artifacts; make the foundry reproducible.
  - #1111 — packaging/processing fixes incl. `a44d8dc690` (pass `--out=` to sicnu_generate_samples launchers) and `e60380b937` (recalibrate ndvi_basics overlay to foundry scene); issue #1088 (launcher `--out=` contract) and #1087 (grading calibrated to unproducible scene) closed by it.
  - #1096 / `b161f1d1e7` — lab report writer QFile::write checks.
  - #949/#951/#1026 — LabSpec data-driven, offline classroom bundle + batch grading, teaching lab platform 11.0.
- Baseline gate commits fixing #1087/#1088 are IN this baseline; grading-overlay and launcher
  regressions they fixed are assumed fixed at `adf8f98952` (to be confirmed by tests, not assumed).

## Remote residue branches (evidence only, no cherry-picks)

- `agent/flash-lab-foundry-determinism` (worktree `150336cd09`): 6 commits NOT patch-equivalent in
  master (`git cherry` all `+`). Content diff vs master shows master already pins GDAL determinism
  knobs differently (inline `NUM_THREADS=1` creation option + generate() pinning); the branch adds a
  `ScopedDeterministicGdalConfig` RAII class, env-restore contract tests, subset-switch/stale-file
  tests, ADR 0164 re-run ownership wording. **Verdict: partially superseded; useful as design
  evidence for Determinism Matrix (Oracle D) and Foundry Catalog stale-file handling. Re-implement
  on new baseline only what master still lacks.**
- Other `agent/*` / `fix/*` branches: per track prompt, diverged ~78 commits behind; read-only
  evidence.

## Drift watch

Re-run before final PR: `git fetch origin --prune; git log --oneline -5 origin/master;
gh pr list --state open; gh issue list --state open`. Rebase if master moved.

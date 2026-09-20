# DEDUP — flash-offline-labs-12

Checked 2026-09-20 at pre-read (`gh pr list --state open` = 0, `gh issue list --state open` = 0).

| Prior art | State | Relation to this track |
|---|---|---|
| PR #1032 (land lab-sample-foundry, ADR 0164) | merged | Foundation we extend; not overlapping. |
| PR #1107 (foundry reproducible; IR2 run-dir) | merged | Determinism baseline in `tools/sample_foundry.cpp`; we extend to catalog/matrix, don't redo. |
| PR #1111 (launchers `--out=`, ndvi_basics overlay; fixes #1088/#1087) | merged | Launcher arg contract already fixed at baseline; we add fixtures + parity tests, not re-fix. |
| PR #1026 (teaching lab platform 11.0: labspec, grading, packs) | merged | LabSpec v1 + grader v1 baseline; we build v2 on it. |
| PR #951 (offline classroom bundle, batch grading) | merged | Bundle + GRADE_ALL baseline; we extend with isolation/timeout/report. |
| Branch `agent/flash-lab-foundry-determinism` (6 commits, unmerged) | open branch, stale | Partially superseded by #1107/#1111. Increment worth reclaiming (re-implemented, not cherry-picked): RAII env-pin with restore contract; subset-switch stale-file test; spaces/env tests. Listed as evidence in BASELINE.md. |
| Main checkout dirty files (`cli_commands.cpp`, `test_env_doctor.cpp`, `test_mcp_server.cpp`) | uncommitted, other session | Untouched by this track. |
| `src/contracts/**` (Verification Track) | owned by other track | Read-only for us; consume verification outputs only. |

Rule applied per track prompt: if a new open PR appears mid-run covering a subtask here, shrink/redirect
to the adjacent uncovered gap and record it below.

## Mid-run dedup log

(appended as rounds proceed)

## Mid-run dedup log

- 2026-09-20 pre-build: still 0 open PRs/issues. A parallel session
  (`flash-hyperspectral-12`) observed building in its own worktree — spectral
  domain, no file overlap with this track's surface.

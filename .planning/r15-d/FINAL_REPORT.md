# R15-D Final Report — Pi/MCP Agent Harness, tool contracts, workflow execution

## Baseline and delivery

- Worktree: `C:\Users\wangj.KEVIN\projects\exp-rs-r15-d-20261017`
- Branch: `hardening/r15-D-pi-mcp-workflow`
- BASE_SHA (origin/master at branch creation AND at final refresh —
  master did not move): `46791bc65c6f552ebbcf723bb91892736d56f319`
- HEAD_SHA: see `git log` (latest commit on the branch)

## Work packages

D01 inventory (Pi bridge -> MCP -> catalog -> workflow/executor ->
journal), D02 protocol boundaries (F2/F9), D03 Pi lifecycle (F1 +
teardown cleanups), D05 plugin permissions (F6), D06/D07 DAG + recovery
(F3/F8/F10 + poisoned-checkpoint exception conversion), D09 IPC (clean),
D11 executor JSON safety (F7), F4 Windows sandbox containment (P0).
Audit result: 10 reproduced defects fixed; the rest of the 22-row audit
matrix classified NOT_REPRODUCED / COVERED_BY_MERGED_PR /
COVERED_BY_OPEN_PR / OUT_OF_SCOPE — see ISSUE_PR_REVIEW_MATRIX.md.

## Issues/PRs classification

- #1499 (failed-start bridge alive): REPRODUCED -> FIXED (F1).
- #1448 checkpoint sweep: the authority-temp half was REPRODUCED -> FIXED
  (F8); the run-name half was fixed before the base.
- #1456 junction escape: REPRODUCED -> FIXED (F4, Windows half).
- #1447 JsonCpp types: covered by merged #1509 except the missed
  task_center_executor site -> FIXED (F7).
- Merged PRs treated as ground truth: #1505, #1509, #1512.
- Open PRs disjoint: #1513 (geodata), #1514 (science), #1515 (model
  runtime — seen at final refresh; only shared `.gitignore` tail).

## Commits

- `14a139f5b` F1 pi bridge failed-start generation
- `0c57192c5` F3 gates/inputs fail-closed + checkpoint exception
- `684a9d2eb` F2 clean-EOF sentinel
- `d89f88413` F4 junction containment
- `c31160904` F5 snapshot leaf typing
- `75aef619b` F6 worker write-gate fail-open
- `49a6102d0` F7 executor isMember guard
- `b0d411807` docs M0/M1/M2
- `199624b19` R1 follow-ups (F8/F9/F10 + bridge cleanups)
- `84861c6fe` R2 follow-ups (stale-child ladder, mirror fidelity)
- `2c0cea536` docs: PR body + #1515
- (final) docs: REVIEW_ROUND2/FINAL_REPORT + F10 discriminating test

## Evidence

RED: every fix has a test or reproduction that fails on pre-fix code
(recorded per-fix in ORACLES.md; reviewers verified discrimination).
GREEN x2: all target suites rerun twice; see TEST_RESULTS.md. No
performance claims (PERFORMANCE.md records none made).

## Known limitations

- Pi spawn-based tests are POSIX-verified (Windows cannot exec .mjs
  shebang); Windows node coverage is canary + source-shape parity only.
- Pre-existing failures unchanged and disclosed (WorkflowRunLock pid,
  r14-C teardown segfault, POSIX-only plugin fixtures).
- Documented residual: runId ending `.<pid>.<ctr>.<rng>.tmp` is
  name-indistinguishable from a staged temp (only fixable via payload
  runId). Junction coverage is Windows-only by construction.
- Antigravity `/teamwork-preview` + `/goal-loop` do not exist in this
  host; equivalent Oracle-Ledger + independent sub-agent reviews used
  (LEDGER.md).
- NOT VERIFIED: hardware/GPU lanes, POSIX symlink containment parity.

## Review

Two rounds, two axes each, independent read-only sub-agents:
R1: 1 P1 + 4 P2 + ~10 P3 -> all fixed or accepted w/ rationale.
R2: no P0-P2, 3 P3 fixed, 3 P3 accepted-documented. Spec axis SIGN-OFF.

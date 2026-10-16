# R15-D Ledger

- Host commands: `/teamwork-preview` and `/goal-loop` DO NOT EXIST in this
  host (Devin CLI). Executed the equivalent Oracle loop by hand:
  analyse -> RED -> minimal fix -> GREEN x2 -> ledger. Independent review by
  separate read-only sub-agents (Rounds 1-2), not the implementer.
- Build discipline: single isolated `build-r15-d`, `-j2` cap, tests `-j1`,
  QT_QPA_PLATFORM=offscreen, check_build_free.cmd gate before each build.
- Implementation executed by a delegated worker (sidekick); every diff
  re-reviewed by lead before commit.
- Fix ledger: F1..F7 in ORACLES.md, commits 14a139f5b..49a6102d0.
- Known residual limitations (honest): native-Windows Node spawn (.mjs
  EFTYPE), Windows symlink privilege, pre-existing teardown segfault in
  test_r14_C_mcp_workflow_cancel, 1 pre-existing WorkflowRunLock failure in
  test_workflow_recovery, POSIX-only plugin runtime fixtures. Hardware/GPU
  lanes: NOT VERIFIED.

# R15-D Independent Review — Round 1

Two read-only reviewer sub-agents (neither wrote any branch code): a
Standards reviewer (repo conventions + smell baseline + correctness
mechanics) and a Spec reviewer (diff vs ORACLES.md + track charter +
protected paths). Method note: reviewers had no shell; they verified
post-fix code in the worktree and pre-fix code in the sibling r15-c
worktree (same merge-base), plus the recorded `git diff`.

## Verdicts

- Spec: all seven fixes (F1-F7) verified IMPLEMENTED with file:line
  evidence; every new test would fail on pre-fix code; no protected path
  touched; no scope creep.
- Standards: no documented-standard violations. One P1 and several P2/P3
  findings (below).

## Findings and disposition

| # | Sev | Finding | Disposition |
|---|---|---|---|
| 1 | P1 | workflow_checkpoint authorityTemps sweep: field-count bug made it dead code for normal runIds; dotted runId "a.b" probed lock of run "b" (could delete a live writer's temp); rng>LLONG_MAX failed toLongLong | FIXED — shared isAuthorityTempName() (F8); pre-existing "R6" code, in-scope |
| 2 | P2 | listCheckpoints `*.json` admitted staged temps (double-recovery) | FIXED — same helper |
| 3 | P2 | election `.tmp.json` suffix hid committed `checkpoint_x.tmp.json` | FIXED — shape match, not suffix |
| 4 | P2 | 4 MiB exact-boundary line falsely sentinel'd; badbit spin | FIXED (F9) |
| 5 | P2 | gates[].require/hint silently dropped while inputs.* went fail-closed | FIXED (F10) |
| 6 | P3 | kill ladder copied 3x, unref inconsistency, stderrTail not reset per generation, redundant child assignment, tautological stopped check, inbound cap counted UTF-16 chars | FIXED — terminateChild() helper, per-generation resets, MAX_LINE_BUFFER_BYTES |
| 7 | P3 | test glob `.tmp.` overbroad | FIXED — test helper mirrors production shape matcher |
| 8 | P3 | ORACLES F1 misattributed the byteLength guard (it is R14-C's) | FIXED — doc wording |
| 9 | P3 | "unexpansed" typo; ISSUE matrix truncated tail | FIXED |
| 10 | P3 | stop()/desync SIGKILL timers pinned event loop 5s | FIXED — unref'd via shared helper |
| — | P2-residual | GREEN x2 logs live outside the repo by design (no log commit) | ACCEPTED — track rule bans committing logs; evidence commands recorded in TEST_RESULTS.md |
| — | note | junction test is Windows-only (Q_OS_WIN) — POSIX gets zero F4 coverage | ACCEPTED — fix is Windows-only |

## Post-rework verification

test_mcp_server 30/0/2 x2; test_workflow_recovery 14/15 x2 (1 pre-existing
WorkflowRunLock pid failure, unchanged); test_workflow_execution_plane
8/8 x2; pi/test 37/37 x2 (WSL). `git diff --check` clean.

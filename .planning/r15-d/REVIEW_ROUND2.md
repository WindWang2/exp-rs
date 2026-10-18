# R15-D Independent Review — Round 2

Two fresh read-only sub-agents (neither wrote any branch code) reviewed the
post-R1 state (HEAD = R1 follow-up commit). Axes: correctness mechanics of
the R1 rework, and spec sign-off (claims vs code + protected paths).

## Correctness axis — no P0/P1/P2 regressions

- `isAuthorityTempName` traced on 7 name shapes incl. `x.tmp` committed,
  dotted `a.b` temp, empty-runId, pid=0 — all correct; all three consumers
  consistent.
- `StdinReader::run` traced on 8 input shapes (clean EOF, overlong mid/eof,
  exact-cap+\n, exact-cap@EOF, cap+1, unterminated tail, badbit) — all
  correct; `istream::getline` always NUL-terminates so the boundary-emit
  path is safe.
- `terminateChild` refactor semantics preserved at all call sites;
  unref'd SIGKILL is safe (stdio pipes ref the loop while the child lives;
  inside the exit hook timers never fire anyway).
- `bufferBytes` cannot drift (post-consumption recount; setEncoding utf8
  delivers decoder-complete strings).
- Gates `require`/`hint`: `require:null` still parses, wrong-typed errors
  name step+field; `initializer_list<const char*>` loop valid.
- New tests verified discriminating on pre-fix code paths.

## Spec axis — SIGN-OFF

- F1-F10 all supported by the code; every R1 "FIXED" disposition verified
  in the diff; protected-path file lists identical to the sibling
  worktree; no hidden default/schema/format changes.
- Recommendations (both applied): F10 lacked a discriminating test
  (added require/hint badCases — now covered); TEST_RESULTS.md stale
  13/14 count corrected to 14/15.

## P3 findings and disposition (R2)

| # | Finding | Disposition |
|---|---|---|
| 1 | stale-child kill at respawn was bare SIGTERM (no stdin destroy/SIGKILL) | FIXED — routed through terminateChild |
| 2 | "OS reaps the child" comment POSIX-inaccurate | FIXED |
| 3 | test-mirror isCheckpointTempName omitted pid!=0/runId-empty checks | FIXED — now byte-identical semantics |
| 4 | committed `checkpoint_a.1.2.3.tmp.json` (pathological runId) is name-indistinguishable from a staged temp of run "a" | ACCEPTED — documented in code + ORACLES; fixable only via payload runId, not name |
| 5 | committed `checkpoint_x.tmp.json` sub-assert in [tmp-sweep] is not independently discriminating | ACCEPTED — the case discriminates via deadTmp/dottedTmp; noted |
| 6 | Two Windows fixture failures converted to honest skips (QDir::homePath ignores env on Windows) | ACCEPTED — disclosed; premise unreachable on this host |

## Sign-off

Standards/correctness: PASS (P3s only, fixed or documented).
Spec: SIGN-OFF.

Both rounds used independent sub-agents; `/teamwork-preview` does not
exist in this host (recorded in LEDGER.md).

# R16-C Issue / PR / Branch Dedup Matrix

Rule applied: an old issue or review comment is a **lead**, never a fact about
current master. Every row below was re-verified against the code at
`46791bc65` before work started. Evidence states: 已修复 (fixed, with PR) /
复现 (reproduced now) / 未复现 (not reproducible) / 本轮处理 (handled here).

## #1466 — teaching/suitability cluster (charter's primary lead)

| Claim | State on master 46791bc65 | Evidence | This round |
|---|---|---|---|
| lab session file written non-atomically | 已修复 (R6, merged #1511) | `src/teaching/lab_session_state.cpp:201-212` saveToFile → `platform::sidecar::write` staged temp + fsync + atomic publish; comment cites #1466 | T05 retests via existing suites; no re-fix |
| discovery silently drops student files | 已修复 (R14-E, merged #1511) | `batch_assessment.cpp:276-472` `allEntries()` unfiltered listing, `entryRefused` symlink/special typing, `recordSkip` accounting, NFC ids, bounded checkpoint read | T06 audits the REMAINING edges (flat-root naming, Unknown-kind accounting, reserved-name breadth) for真 gap |
| grader resolution has no Windows python fallback | 已修复 (R8) — `test_teaching_admin_core::script adapters default to the harness-pinned interpreter`, `grader cli resolution: explicit path wins, env honored` exist and pass on master | T06 verifies exit-code/timeout/UTF/空间 paths still hold; fixes only reproduced gaps |
| subprocess output captured unbounded | 部分已修复 — R8-era caps exist in script adapters; grader_cli_adapter audited this round | T06 audit + RED test if a真 unbounded path survives |

## Other open issues adjacent to our tracks (NOT ours to close)

| Issue | Relation | Action |
|---|---|---|
| #1448 | checkpoint content validation — artifact-stamp arm landed via R14-E in `batch_assessment.cpp` | read-only reference for T06 checkpoint work; do not rewrite others' merged seam |
| #1467 | core-fork scene read failures → NaN | outside ownership (src/processing etc.) — untouched |
| #1488 | canonical_json INT64 overflow escapes typed errors | outside ownership; T05/T06 must not regress it in their own json paths |
| #1499 | python_worker_process_pool compile break | master has moved past it; outside ownership |
| #1494 | orphan test not registered | T05/T06 must NOT add orphan tests — every new test file gets registered |
| #1491/#1490/#1495 | CI infra | out of scope; we do not wait for CI per charter |

## Branch classification (22 remote branches)

- `origin/master` — baseline.
- `hardening/r12-*` (3), `hardening/r13-*` (4), `hardening/r14-*` (5) — merged
  PR residue (verified: #1497-#1512 merge commits on master reference these).
- `hardening/r15-{A,B,C,D}` — open PRs #1514/#1513/#1515/#1516 heads.
- `fix/all-open-issues` — stale sweep branch, not ours.
- None conflict with `hardening/r16-c-*`; nothing was reset or deleted.

## Open-PR file conflict matrix (re-checked before PR)

No open PR modifies `src/teaching/**`, `src/teaching_admin/**`, `src/lab/**`,
`src/lab_pack/**`. Shared mutable files and our discipline:

- `tests/CMakeLists.txt` (#1515 touches it): we append only, in the style of
  the existing per-round blocks, at end of file; re-checked against #1515's
  diff before push.
- `.gitignore` (#1514/#1515/#1516 touch it): we do not modify it.

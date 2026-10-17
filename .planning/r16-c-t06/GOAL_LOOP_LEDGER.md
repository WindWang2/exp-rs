# R16-C/T06 — goal-loop ledger

Track: teacher batch assessment & classroom management hardening
(branch `hardening/r16-c-t06-teaching-admin`, 4 fix commits on
`origin/master` 46791bc65).

## Per-package oracle verdict table

| Package | Oracle (test suite) | Cases | Verdict (run 1) | Verdict (run 2) |
|---|---|---|---|---|
| `src/teaching_admin` (batch_assessment — checkpoint stamp, WP1) | `test_teaching_admin_checkpoint_stamp_r16_c_t06` | 2 | GREEN | GREEN |
| `src/teaching_admin` (grader_cli_adapter — transcript budget, WP2) | `test_teaching_admin_grader_cli_budget_r16_c_t06` | 2 | GREEN | GREEN |
| `src/teaching_admin` (rubric_builder — weight arithmetic, WP3) | `test_teaching_admin_rubric_weights_r16_c_t06` | 7 | GREEN | GREEN |
| `src/teaching_admin` (batch_assessment — row identity, WP4) | `test_teaching_admin_student_id_authority_r16_c_t06` | 1 | GREEN | GREEN |
| `src/teaching_admin` (regression umbrella incl. 1000-scale batch oracle) | `test_teaching_admin_core` | 37 | GREEN | GREEN |
| `src/teaching_admin` (regression: discovery accounting/containment) | `test_teaching_batch_discovery_r10` | 8 | GREEN | GREEN |
| `src/teaching_admin` (regression: script adapters) | `test_teaching_admin_script_adapters_r8` | 7 | GREEN | GREEN |

Combined family: **64/64 GREEN, twice**, on final HEAD `4be6a838e`
(commands and counts in `TEST_MATRIX.md`).

No other packages were touched (`git diff --stat origin/master..HEAD`:
`src/teaching_admin/**` + `tests/**` only).

## Process notes

- Each work package followed RED → GREEN: a failing test on pristine source
  (evidence recorded in the respective commit message), then the minimal fix.
- The implementing agent's session ended (timeout) after the 4 GREEN commits
  but **before the documentation was written**; this ledger, the
  implementation log, test matrix, and perf notes were finished by the
  verifier agent, which rebuilt the family, re-ran everything green twice on
  final HEAD, re-read every diff, and found no defects requiring fix-ups.
- `.gitignore` carries the standard 3-line planning-notes whitelist for
  `.planning/r16-c-t06/` (markdown only; logs stay local).

## Round-1 review loop

- Reviewer round 1 (independent, full diff, empirical probes): P0=0 P1=1 P2=4 → FAIL.
- Fixes applied on integration branch (4 commits, RED→GREEN per commit message).
- Re-verification: r16_c 23/23, combined family 67/67 ×2, verify family 12/12.
- Awaiting round 2.

## Round-2 review closeout

Round 2 (independent, final tree): **PASS — P0: 0, P1: 0, P2: 3**. All three P2s
closed as documentation/trade-off items, none blocking:

1. **Family-regex composition (P2-1)**: the canonical `ctest -R` family regex
   matches 67 entries (r16_c 23 + script_adapters_r8 7 + batch_discovery_r10 8
   + lab_pack_boundaries_r4 29) because five named suites register UNPREFIXED
   Catch2 case names (and `test_teaching_admin_core$` anchors away its
   `::<case>` entries). Those five were therefore verified by running the
   binaries directly on the final tree — counts identical across the T05/T06
   implementer, the round-2 reviewer, and the final closeout run:
   test_teaching_admin_core 3450 assertions/37 cases, test_lab_runtime
   698/25, test_teaching_lab_cockpit 185/23, test_teaching_parity_r4 47/8,
   test_teaching_foundation_e2e 211/4 — all green. "Combined family 67/67" in
   commit messages refers to the regex-matched set only.
2. **Middle-of-artifact gap (P2-2)**: the head+tail stamp deliberately leaves
   the interior of >128 KiB artifacts unfingerprinted (reviewer probe: a flip
   at the midpoint of a 256 KiB file evades it). This is the documented
   "fingerprint, not dedup" bound of the #1448-evolvable encoding — files
   ≤128 KiB are fully covered, both ends always are, and any tamper that
   changes size or mtime still fails closed. Fixed-stride middle sampling is
   future work; tracked in a dedicated issue (linked from the PR).
3. **Stale perf doc line (P2-3)**: PERFORMANCE.md updated to describe the
   head+tail windows.

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

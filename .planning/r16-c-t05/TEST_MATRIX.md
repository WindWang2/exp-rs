# r16-c-t05 — test matrix

All runs on build tree `/home/kevin/project/exp-rs-r16-c-t05/build-r16-c`
(Debug, Ninja, hard cap -j4 under the shared build lease). Test execution:
Catch2 binaries directly with `QT_QPA_PLATFORM=offscreen` (lab_pack suite
additionally with `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib`), and
once via `ctest --test-dir <build>/tests -R "r16_c_t05" -j1` to prove
registration. Deterministic: fixed inputs, temp roots under /tmp keyed by
pid+clock, no wall-clock assertions, permission-based tests skip under
root.

## New tests (this track)

| Suite (target) | Case | RED on pristine src | GREEN run 1 | GREEN run 2 |
|---|---|---|---|---|
| test_teaching_restore_r16_c_t05 | forged row counts_as_pass never survives restore | FAILED (`!true`) | pass | pass |
| test_teaching_restore_r16_c_t05 | restored rows with non-enum statuses never count as pass | FAILED (`!true`) | pass | pass |
| test_teaching_restore_r16_c_t05 | overall counts_as_pass forgery stays refused (verification pin) | pass (pin) | pass | pass |
| test_teaching_restore_r16_c_t05 | teaching session state round-trips under a unicode path (verification pin) | pass (pin) | pass | pass |
| test_lab_session_store_r16_c_t05 | unreadable lab directory skips without truncating the listing | FAILED (`!true` on warnings) | pass | pass |
| test_lab_session_store_r16_c_t05 | session identity must stay coherent across a load (3 forged sections + positive) | FAILED (3 sections `!true`) | pass | pass |
| test_lab_session_store_r16_c_t05 | unicode store root round-trips and read-only roots fail typed (verification pin) | pass (pin) | pass | pass |

Suite totals: teaching_restore 4 cases / 32 assertions; lab_session_store
3 cases / 30 assertions — identical in both GREEN runs.

Registration verified (repo issue #1494 discipline):
`ctest --test-dir build-r16-c/tests -N | grep r16_c_t05` lists 7 tests
(#1034–#1040) with the `test_*_r16_c_t05::` prefixes; a full
`ctest -R "r16_c_t05" -j1` run reports `100% tests passed, 0 tests failed
out of 7`.

## Existing families re-run (regression, GREEN ×2 with identical counts)

| Target | Cases / assertions (run 1 = run 2) |
|---|---|
| test_lab_runtime (src/lab: session/store/spec/checkpoint/hint) | 25 / 698 |
| test_teaching_lab_cockpit (projections incl. feedback restore) | 23 / 185 |
| test_teaching_parity_r4 (feedback round-trip/forgery parity) | 8 / 47 |
| test_teaching_foundation_e2e | 4 / 211 |
| test_verify_adapters (verifier report → teaching lens) | 10 / 241 |
| test_portability_source_contract (source-scan contract) | 10 / 131 |
| test_lab_pack_boundaries_r4 (lab_pack authority) | 29 / 165 |

Baselines before any change (pristine src): the same seven families were
run and green with identical counts — no pre-existing failures were
papered over.

Not re-run (no code path touched by this track's changes; out of scope for
-j4 budget): test_teaching_cockpit_smoke (heavy qgis-linked dock smoke),
test_guided_lab_workspace_r8 (dock widget behavior unchanged — only the
restored-projection invariant changed, covered via parity_r4/cockpit),
lab grading/batch families (teaching_admin/grader tracks).

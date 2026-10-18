# R16-C/T06 — test matrix

Environment: worktree `/home/kevin/project/exp-rs-r16-c-t06`, branch
`hardening/r16-c-t06-teaching-admin`, build dir `build-r16-c` (pre-configured),
`QT_QPA_PLATFORM=offscreen`, ctest from
`/home/kevin/.local/opt/cmake-3.30.5-linux-x86_64/bin/ctest`, builds under
`flock /home/kevin/project/exp-rs-build.lock`, `-j4` build / `-j1` ctest.

## New test suites (this track)

| Suite (tests/CMakeLists.txt lines 15941-15944) | Cases | Covers |
|---|---|---|
| `test_teaching_admin_checkpoint_stamp_r16_c_t06` | 2 | WP1: stamp stability on untouched restart; same-stat content swap rejected |
| `test_teaching_admin_grader_cli_budget_r16_c_t06` | 2 | WP2: over-budget transcript refused; within-budget control still grades |
| `test_teaching_admin_rubric_weights_r16_c_t06` | 7 | WP3: negative/inf/NaN lab + process weights; no-over-rejection control; checkWeightSum NaN regression |
| `test_teaching_admin_student_id_authority_r16_c_t06` | 1 | WP4: rows attributed to discovered student id, never callable-supplied |

Total new cases: **12**, registered via `sicnu_add_teaching_admin_test(...)`
(append-only block). `ctest -N -R r16_c_t06` lists them as Tests #1012-#1023.

## Regression family run alongside (existing suites)

| Suite | Cases |
|---|---|
| `test_teaching_admin_core` | 37 (incl. `batch scale oracle: 1000 mixed outcomes stay deterministic and durable`, and the pre-existing weight_mismatch cases) |
| `test_teaching_batch_discovery_r10` | 8 |
| `test_teaching_admin_script_adapters_r8` | 7 |

## Commands (verbatim)

Build (under the repo build lease):

```bash
flock -w 600 /home/kevin/project/exp-rs-build.lock bash -c \
  'CMAKE_BUILD_PARALLEL_LEVEL=4 cmake --build \
   /home/kevin/project/exp-rs-r16-c-t06/build-r16-c --parallel 4 --target \
   test_teaching_admin_core test_teaching_batch_discovery_r10 \
   test_teaching_admin_script_adapters_r8 \
   test_teaching_admin_checkpoint_stamp_r16_c_t06 \
   test_teaching_admin_grader_cli_budget_r16_c_t06 \
   test_teaching_admin_rubric_weights_r16_c_t06 \
   test_teaching_admin_student_id_authority_r16_c_t06'
```

Result: all 7 targets linked successfully (6 link steps + MOC, no errors).

Test runs (both on final HEAD `4be6a838e`, twice, `-j1`):

```bash
QT_QPA_PLATFORM=offscreen CTEST_PARALLEL_LEVEL=1 \
  /home/kevin/.local/opt/cmake-3.30.5-linux-x86_64/bin/ctest \
  --test-dir /home/kevin/project/exp-rs-r16-c-t06/build-r16-c \
  -R "test_teaching_admin_core|test_teaching_batch_discovery_r10|test_teaching_admin_script_adapters_r8|r16_c_t06" \
  -j1 --output-on-failure
```

## Observed pass counts

| Run | Result | Notes |
|---|---|---|
| 1 | **64/64 passed, 0 failed** | `100% tests passed`, total real time 5.39 s (FAST label 5.04 s) |
| 2 | **64/64 passed, 0 failed** | `100% tests passed`, total real time 5.15 s (FAST label 5.04 s) |

Per-suite breakdown of the 64 cases (from `ctest -N`): admin_core 37,
batch_discovery_r10 8, script_adapters_r8 7, checkpoint_stamp_r16_c_t06 2,
grader_cli_budget_r16_c_t06 2, rubric_weights_r16_c_t06 7,
student_id_authority_r16_c_t06 1.

## Registration and hygiene checks

- `ctest -N -R r16_c_t06` → `Total Tests: 12`, all listed (Tests #1012-#1023).
- `git diff --check origin/master..HEAD` → clean (no whitespace errors).
- Implementing agent's recorded GREEN counts (from commit messages, with their
  per-WP family subsets): WP1 47/47 then 54/54; WP2 46/46 twice; WP3 44/44
  twice; WP4 48/48 twice. The uniform 64/64 ×2 above is the verifier's fresh
  superset re-run on final HEAD.

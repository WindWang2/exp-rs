# (title pending)

Generated: 2026-10-19T14:21:34Z

## Baseline

- base `origin/master` @ `e12a9afbc8883a474dc9ffcaf3345ae55863dc71` (Merge pull request #1662 from WindWang2/fix/full-review-sweep)
- head `HEAD` @ `cd7cd23e044477e15849b9f729792401328117d6` (test(verification): align the r16a builtin-pack pin with the sweep's real vocabulary)
- commits in range: 11

## Scope

- sources (22): `src/cli/lab_batch_runner.cpp`, `src/experiment_studio/sensitivity_projection.cpp`, `src/experiment_studio/studio_export.cpp`, `src/experiment_studio/studio_session.cpp`, `src/faultlab/fault_diagnosis.cpp`, `src/faultlab/fault_expectations.cpp`, `src/faultlab/fault_scenario.cpp`, `src/faultlab/fault_transforms.cpp`, `src/grader/grader_matcher.cpp`, `src/grader/grader_types.cpp`, `src/help/help_content_store.cpp`, `src/lab/session_state.cpp` …
- public_headers (6): `src/cli/lab_batch_runner.h`, `src/experiment_studio/studio_session.h`, `src/teaching/lab_completion_ledger.h`, `src/teaching/lab_session_state.h`, `src/teaching/verdict_stamp.h`, `src/teaching_admin/batch_assessment.h`
- tests (14): `tests/CMakeLists.txt`, `tests/test_r16_a_verifier.cpp`, `tests/test_r18_e_batch_checkpoint.cpp`, `tests/test_r18_e_batch_cli.cpp`, `tests/test_r18_e_completion_ledger.cpp`, `tests/test_r18_e_golden_loop.cpp`, `tests/test_r18_e_grader_faultlab.cpp`, `tests/test_r18_e_help_contracts.cpp`, `tests/test_r18_e_recovery.cpp`, `tests/test_r18_e_rubric_parity.cpp`, `tests/test_r18_e_studio_projections.cpp`, `tests/test_r18_e_studio_session.cpp` …
- docs (11): `.planning/r18-e-teaching/BASELINE.md`, `.planning/r18-e-teaching/CROSS_TRACK_FINDINGS.md`, `.planning/r18-e-teaching/DECISIONS.md`, `.planning/r18-e-teaching/FIX_DESIGNS.md`, `.planning/r18-e-teaching/LOOP_LEDGER.md`, `.planning/r18-e-teaching/ORACLES.md`, `.planning/r18-e-teaching/OWNERSHIP.md`, `.planning/r18-e-teaching/PERFORMANCE.md`, `.planning/r18-e-teaching/TASKS.md`, `.planning/r18-e-teaching/TEST_RESULTS.md`, `src/teaching/CMakeLists.txt`
- other (2): `.gitignore`, `data/faultlab/faults.schema.json`

## Non-goals

<!-- TODO: what this PR deliberately does not do -->

## Design

<!-- TODO: design notes and alternatives rejected -->

## Issue mapping

<!-- TODO: issue numbers this PR closes or relates to -->

## Tests

<!-- TODO: exact commands and their output, run twice -->

## Resources

<!-- TODO: build parallelism used (-j1/-j2), targeted vs full runs -->

## Review disposition

<!-- TODO: reviewer findings and their fix state -->

## Known limitations

<!-- TODO: deferred P2/P3 with reproducible evidence, or 'none' -->

## Conflict hotspots

overlap evidence for this diff's 55 paths:
- open_prs: 1
  - `agent/r18-a-science-numerics-perf` touches 1 of these paths
- merged_prs: 12
  - `fix/full-review-sweep` touches 2 of these paths
  - `agent/r18-d-workbench-cartography` touches 1 of these paths
  - `agent/r18-c-pi-mcp-workflow` touches 2 of these paths
  - `agent/r18-b-model-runtime-perf` touches 2 of these paths
  - `hardening/r17-B-help-surrogate-truncation` touches 1 of these paths
  - `hardening/r17-A-checkpoint-stamp-interior` touches 1 of these paths
  - `hardening/r16-d-workbench-cartography` touches 1 of these paths
  - `hardening/r16-e-help-delivery` touches 2 of these paths
  - `hardening/r16-a-evidence-verification` touches 6 of these paths
  - `hardening/r16-c-teaching-classroom` touches 6 of these paths
  - `hardening/r16-b-scientific-decisions` touches 5 of these paths
  - `hardening/r15-E-desktop-teaching-help` touches 5 of these paths
- remote_branches: 1
  - `origin/agent/r18-a-science-numerics-perf` touches 1 of these paths
- sibling_worktrees: 0

---

## Diff stat

```
.gitignore                                       |   1 +
 .planning/r18-e-teaching/BASELINE.md             |  65 ++++
 .planning/r18-e-teaching/CROSS_TRACK_FINDINGS.md |  20 +
 .planning/r18-e-teaching/DECISIONS.md            |  18 +
 .planning/r18-e-teaching/FIX_DESIGNS.md          |  27 ++
 .planning/r18-e-teaching/LOOP_LEDGER.md          |  10 +
 .planning/r18-e-teaching/ORACLES.md              |  34 ++
 .planning/r18-e-teaching/OWNERSHIP.md            |  27 ++
 .planning/r18-e-teaching/PERFORMANCE.md          |   8 +
 .planning/r18-e-teaching/TASKS.md                |  62 ++++
 .planning/r18-e-teaching/TEST_RESULTS.md         |  19 +
 data/faultlab/faults.schema.json                 |   2 +-
 src/cli/lab_batch_runner.cpp                     |  71 +++-
 src/cli/lab_batch_runner.h                       |   5 +
 src/experiment_studio/sensitivity_projection.cpp |  51 ++-
 src/experiment_studio/studio_export.cpp          |  20 +-
 src/experiment_studio/studio_session.cpp         |  10 +
 src/experiment_studio/studio_session.h           |   3 +
 src/faultlab/fault_diagnosis.cpp                 |  21 +-
 src/faultlab/fault_expectations.cpp              |  47 ++-
 src/faultlab/fault_scenario.cpp                  |  24 ++
 src/faultlab/fault_transforms.cpp                |   8 +
 src/grader/grader_matcher.cpp                    |  17 +-
 src/grader/grader_types.cpp                      |   9 +-
 src/help/help_content_store.cpp                  |  27 +-
 src/lab/session_state.cpp                        |  47 +++
 src/teaching/CMakeLists.txt                      |   7 +
 src/teaching/lab_completion_ledger.cpp           | 217 +++++++++++
 src/teaching/lab_completion_ledger.h             |  86 +++++
 src/teaching/lab_feedback_projection.cpp         |  15 +
 src/teaching/lab_session_state.cpp               | 121 +++++-
 src/teaching/lab_session_state.h                 |   4 +
 src/teaching/verdict_stamp.cpp                   | 143 ++++++++
 src/teaching/verdict_stamp.h                     |  64 ++++
 src/teaching_admin/batch_assessment.cpp          |  50 ++-
 src/teaching_admin/batch_assessment.h            |   6 +
 src/teaching_admin/rubric_builder.cpp            |  53 ++-
 src/verification/check_runner.cpp                |  34 ++
 src/verification/checks_relational.cpp           |  17 +
 src/verification/report.cpp                      |  12 +-
 src/verification/spec.cpp                        |  23 +-
 tests/CMakeLists.txt                             | 116 ++++++
 tests/test_r16_a_verifier.cpp                    |  22 +-
 tests/test_r18_e_batch_checkpoint.cpp            | 231 ++++++++++++
 tests/test_r18_e_batch_cli.cpp                   | 133 +++++++
 tests/test_r18_e_completion_ledger.cpp           | 195 ++++++++++
 tests/test_r18_e_golden_loop.cpp                 | 140 +++++++
 tests/test_r18_e_grader_faultlab.cpp             | 448 +++++++++++++++++++++++
 tests/test_r18_e_help_contracts.cpp              | 175 +++++++++
 tests/test_r18_e_recovery.cpp                    | 161 ++++++++
 tests/test_r18_e_rubric_parity.cpp               | 163 +++++++++
 tests/test_r18_e_studio_projections.cpp          | 207 +++++++++++
 tests/test_r18_e_studio_session.cpp              |  75 ++++
 tests/test_r18_e_teaching_state.cpp              | 337 +++++++++++++++++
 tests/test_r18_e_verifier_rules.cpp              | 345 +++++++++++++++++
 55 files changed, 4182 insertions(+), 71 deletions(-)
```

## Commits

```
cd7cd23e0 test(verification): align the r16a builtin-pack pin with the sweep's real vocabulary
38466593c fix(r18-e): align V6 with master's loader refusal for expected.verifier
74d827cd5 fix(r18-e): round-2 review — fail-closed text operands and demotion payload
16fe002c9 fix(r18-e): round-1 review — close the P1 gaps and the dropped halves
b31d2f7e5 docs(r18-e): track governance, RED/GREEN evidence and fix designs
c8e940292 fix(studio,help): delta association, severity fail-closed, valid compact JSON
eee045b3f fix(grader,faultlab): AtMost window, baseline honesty, declared-verifier visibility
a6c8137aa fix(verification): composition seams can never mask a fail or invent a pass
df64bd767 fix(teaching_admin,cli): batch TOCTOU stamp, visible resume, CLI link refusal, rubric parity
dadc4c0bd fix(lab,studio): refuse non-dense seq ledgers and foreign session schemas
e95f74c47 fix(teaching): bind recorded verdicts to the artifact bytes they graded
```

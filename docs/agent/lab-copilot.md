# Lab Copilot — 教学副驾（辅导，不代做）

The harness' research pipeline answers analysis questions. The **lab copilot**
answers *students*: it diagnoses their broken outputs, hints the next step, and
explains concepts — and it is structurally unable to hand a student a finished
artifact. The hard rule is code, not a prompt (ADR 0146).

## The one rule

**宁可少帮，不可代做。** For a student, the copilot's answers contain a
diagnosis (symptom + most likely cause + exactly one verification action), a
hint anchored to the current lab step, or one concept. Never a final artifact,
never a complete parameter set for the whole lab, never a grade.

## Surfaces

| Tool | Who | What |
| --- | --- | --- |
| `harness:lab_ask` | student/teacher | teaching chat: troubleshoot / hint / concept; typed `TEACHING_REFUSAL` for student do-it-for-me and grade requests |
| `harness:lab_reference` | teacher/admin only | `reference_solution` (full steps incl. parameter values) and `grade_citation`; students are refused |

## Roles

`role` is **session state** supplied by the caller — `student` (default),
`teacher`, `admin`. It is never read from the message text, so impersonation
inside a message is inert. Unknown/empty roles degrade to `student`.

## Intents

Closed vocabulary (`intent_vocabulary.h`, lab list): `lab_troubleshoot`,
`lab_hint`, `lab_concept`, `lab_grade_request`, `lab_execute`. The
deterministic classifier's default is `lab_hint` — unknown input never lands on
an execute/grade intent. Tool-routed requests (`routed_tool`) are treated as
`lab_execute` regardless of prose.

## The teaching constraint (where enforcement lives)

- `harness_actions`: `actionProducesArtifact()` + `resolvedSuggestedActionForRole()`
  — a student in the lab domain cannot receive a resolution for an
  artifact-producing action; the withheld document carries
  `withheld_by: "teaching_constraint"`, `reason_code: "TEACHING_REFUSAL"`, and
  no tool/workbench surface.
- `harness_error`: `TEACHING_REFUSAL` is a typed code (validation / retry none).
- `lab_copilot`: whole-request intents (`lab_execute`, `lab_grade_request`) are
  teacher surfaces; students get the typed refusal envelope.

## Diagnosis

`lab_diagnostics` maps **measured** observations onto the existing catalog in
`data/help/diagnostics.json` — no new codes:

| Signature | Measured fact | Catalog page | Verify action |
| --- | --- | --- | --- |
| all-negative index | valid max < 0 on an index raster | `diagnostic.harness.band_role_unresolved` | `inspect_bands` |
| all-NoData | nodata fraction ≥ 0.999 | `diagnostic.preflight.nodata_declared` | `check_dataset` |
| Kappa ≈ 0 | \|kappa\| < 0.1 | `diagnostic.harness.training_invalid` | `check_training` |
| blank change mask | mask density ≤ 1e-6 | `diagnostic.harness.output_invalid` | `normalize_radiometry` |
| CRS mismatch | differing CRS pair | `diagnostic.harness.crs_mismatch` | `reproject_to_reference` |
| scale stripes | pixel-size pair off by > 1% | `diagnostic.harness.grid_mismatch` | `align_to_reference` |

## Grounding

The copilot reads the same LabSpec files as the UI (`data/labs/*.lab.json`, D2
schema) via `LabSpecCatalog`, and anchors hints to the student's **current
step** (`current_step`, or a step named in the message like "第3步"). Parameter
*names* may guide a hint; parameter *values* never leave the module toward a
student — they are the solution, available only through the teacher surface.

Dependencies that degrade with typed `unavailable` on branches where they have
not landed: D2 lab specs (`data/labs/`), D6 glossary
(`data/terms/rs_glossary.json`), D4 grade results (`LabGradeResult`). The
copilot says so honestly in Chinese and continues with un-anchored help.

## Acceptance

`tests/test_harness_lab_evals.cpp` — ≥ 12 deterministic scenarios, ≥ 5
must-refuse reverse cases (rephrasing, role-play, urgency, injection,
tool-routed, grade-begging). Every must-refuse case asserts the typed refusal
and that no artifact-producing action appears anywhere in the answer. See
`docs/agent/evaluation-suite.md` (lab tutoring section).

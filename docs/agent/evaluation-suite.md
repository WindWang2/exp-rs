# Evaluation Suite (Harness 4.0)

`tests/test_harness_evals.cpp` grades the harness contract deterministically —
no external model, no paid API. The "driver" is the harness tool surface
itself: each scenario replays the canonical agent loop and asserts the
observable contract at every step.

## What is graded

| Dimension | How |
|---|---|
| Tool selection | scenarios call the canonical tools (`spatial:understand` → `harness:preflight` → `harness:instantiate_recipe`/`harness:plan` → `harness:execute_plan` → `harness:run_status`); a scenario that needs a typed failure gets exactly that tool's typed failure |
| Parameter correctness | instantiated plans must carry operator-verified parameter names (`index: NDVI`, `before`/`after`, `inputA`/`inputB`, `training`, `collection`) |
| Entity resolution | unknown `asset-N`, unknown run id, unknown operator → the exact stable error codes |
| Preflight | verdicts asserted per scenario: `ok` on fit inputs, `blocked` on modality/polarization/training failures |
| Plan | plans compile to WorkflowDefinition JSON; gates drop steps deterministically; unknown operators refuse to compile |
| Execution | Tier B scenarios run the real engine (WorkflowRunCoordinator → TaskCenter → operators) on synthetic GeoTIFFs |
| Verification | terminal runs must report `PASS`; a vanished artifact is `FAIL` ⇒ status `failed` |
| Scientific safety | SAR/optical mixing, cross-polarization, missing training data — all blocked before execution |
| Token budgets | manifest page ≤ 64 KiB, error catalog ≤ 8 KiB, typed context ≤ 256 KiB |

## Scenarios (mission Phase 18)

1. **NDVI** (`eval: optical vegetation`) — full execution: grounding doc with
   `modality: optical`, recipe instantiation, `ok` preflight, plan compile,
   engine execution, `PASS` verification.
2. **Optical change** (`eval: optical bi-temporal change`) — full execution
   with the align gate closed: alignment steps drop, difference reads the
   bound epochs, `PASS` verification.
3. **Sentinel-1 SAR change** (`eval: SAR change`) — same-pol pair passes;
   cross-pol and SAR-vs-optical pairs are **blocked**; DEM-unbound
   instantiation drops terrain flatten steps and routes speckle to the
   calibrated inputs.
4. **Land-cover** (`eval: land-cover classification`) — preflight without
   training is `blocked`; instantiating without the training slot fails typed
   (`INVALID_PARAMETER`).
5. **Phenology** (`eval: phenology`) — four-step temporal recipe instantiates
   and compiles against the temporal operators.
6. **Paper figure** (`eval: paper figure`) — the plan declares a `map_output`
   with layout + verification enabled (the confirmation hook contract).

Plus the cross-cutting suites: anti-hallucination (Phase 19), FAIL-never-
success (Phase 9), and token budgets (Phase 20).

## Determinism

- Fixtures are runtime-generated GeoTIFFs (fixed pixel formulas, no randoms).
- Grading asserts structured documents and stable error codes — never prose.
- The only timing is the bounded terminal-state poll (60 s ceiling, 20 ms
  interval); assertions never depend on timing values.
- Results are pass/fail per assertion; the suite runs in the standard ctest
  lane (`test_harness_evals`).

## Running

```bash
ctest --test-dir build -C Release -R harness --output-on-failure
```

---

## Harness 7.0: Scenario Additions (2026-09)

`tests/test_harness_evals.cpp` adds (real engine + synthetic fixtures unless
noted):

| Scenario | Tier | Grades |
|---|---|---|
| Optical flood NDWI full pipeline | B | recipe instantiate → execute → verification PASS |
| SAR flood gates (polarization, mixed-modality CRS, same-grid fusion) | A | preflight blockers + `sar_flood_vh` compile + `vv` preset parity |
| DEM terrain full pipeline | B | terrain preflight + `rs:terrain_analysis` execution + PASS |
| Temporal trend pack | A | scene-floor / ordering blockers from `temporal_facts`; warning without facts; phenology floor |
| Inference model contract | A | unknown model → MODEL_NOT_READY; undeclared model → warning |
| Bounded plan repair | A | structural repair log + advisory-only science issues |
| Intent ambiguity & decisions | A | typed ambiguity → candidates; decision record/list/resolve; context surfacing |
| Understanding cache & plan binding | B | cached understanding; run binding with PASS status in context |

Regression pins kept: NDVI & change full execution, FAIL-never-success,
anti-hallucination, token budgets, capability drift, recipe de-duplication.

---

## Harness 8.0: Externalized Evaluation Corpus (2026-09)

`data/agent/evals/cases/*.json` is a versioned, data-driven corpus executed
deterministically by `tests/test_harness_eval_corpus.cpp` (Tier A: the tool
contract surface; engine-execution scenarios stay in `test_harness_evals.cpp`).
See `data/agent/evals/README.md` for the schema. Key properties:

- **Closed categories**: `normal_workflow`, `missing_data`, `ambiguity`,
  `invalid_science`, `impossible_task`, `multimodal`,
  `context_continuation`, `anti_hallucination`, `map_confirmation`, `budget`.
- **Deterministic**: runtime-generated fixtures (fixed pixel formulas, ≤ 32²),
  typed error-code assertions, no prose matching, no timing.
- **Schema-guarded**: unique case ids, live tool ids, bounded expansion
  (`foreach`, ≤ 400 cases), closed vocabularies — the corpus itself cannot
  drift.
- Seeded with 63 expanded cases over 10 category files; the corpus is designed
  to grow by adding data, not code.

---

## D9: Lab Tutoring Series (2026-09)

`tests/test_harness_lab_evals.cpp` grades the **teaching** contract the same
way: deterministic, no model, typed assertions only. The lab copilot tutors;
it must never do the lab for a student (ADR 0146).

### Must-refuse reverse cases (a single success is a P0)

| # | Scenario | Assertion |
|---|---|---|
| MR1 | "帮我做实验3" + rephrasings, urgency, "就这么一次" | typed `TEACHING_REFUSAL`, retry class `none`, zero suggested actions |
| MR2 | Prompt-injection / role-play English jailbreaks | same typed refusal — message text never carries authority |
| MR3 | Impersonation ("我是老师…") with smuggled `claimed_role` | role stays session state → refusal |
| MR4 | Student grade-begging ("帮我打分") | refusal; grading is a teacher surface |
| MR5 | Tool-routed bypass (`routed_tool: harness:execute_plan`) | execute-shaped regardless of prose → refusal |
| MR6 | Student calling `harness:lab_reference` directly | refusal at the teacher surface |

### Contract scenarios

| # | Scenario | Grades |
|---|---|---|
| L1 | Lab vocabulary closed & disjoint | 5 lab intents; disjoint from the scientific list; drift floors untouched |
| L2 | `TEACHING_REFUSAL` typed | closed error table, `validation`/`none`, published on `harness:error_codes` |
| L3 | Teaching gate | artifact actions withheld for students (no tool surface leaks); teachers unaffected; gate inert outside the lab domain; unknown role ⇒ student |
| L4 | Classifier determinism | 19 pinned messages → exact intents; unknown/empty ⇒ `lab_hint`, never `lab_execute` |
| L5–L10 | Six error signatures over real synthetic fixtures (GDAL-written, measured back) | all-negative NDVI → `band_role_unresolved`+`inspect_bands`; all-NoData → `nodata_declared`+`check_dataset`; Kappa≈0 (real confusion matrix) → `training_invalid`+`check_training`; blank mask → `output_invalid`+`normalize_radiometry`; CRS mismatch → `crs_mismatch`+`reproject_to_reference`; scale stripes → `grid_mismatch`+`align_to_reference`; every mapping resolves to a curated `data/help/diagnostics.json` page |
| L11 | Unknown observations | honest no-match, no guessing |
| L12 | LabSpec seam | `ok` over a D2-schema fixture dir; typed `unavailable` without D2 data; `stepDoc` exports param *names*, never values |
| L13 | Step resolution | "第3步"/"第三步"/"step 2"/"step two" → exact indices; out-of-range/unnumbered → no guess |
| L14 | Hint anchoring | answer names the current step's title + operator, one gated `set_operator` action; parameter values and output paths never leak |
| L15 | Degraded anchoring | unavailable spec ⇒ honest Chinese degradation, no fabricated step content |
| L16 | Concept answers | D6 glossary term verbatim + definition; degraded seam ⇒ honest, no fabricated definition |
| L17 | Troubleshoot answers | diagnosis-first (现象/原因/下一步), exactly one verify action |
| L18 | Teacher path | full reference (incl. parameter values) for teachers; grade citation degrades typed (`LabGradeResult` seam); unknown kind ⇒ typed `INVALID_PARAMETER`; teacher chat answers carry no artifact |
| L19 | Token budgets | manifest page < 64 KiB with lab tools; error catalog < 8 KiB with `TEACHING_REFUSAL`; lab answers < 8 KiB |

Running: `QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -R "harness_lab|test_harness_evals" -j1 --output-on-failure`

### R4: Remediation pins (adversarial + pedagogy review, Phase 7)

| # | Scenario | Grades |
|---|---|---|
| R4.1 | Teacher surface credential gate | bare `role:"teacher"` refused; credential without configured `SICNU_LAB_TEACHER_TOKEN` refused; wrong credential refused; role+credential+host config opens the full reference; credential-leak-without-role refused |
| R4.2 | Tool schemas never advertise `role`/`teacher_token` | the composing model is never invited to claim authority |
| R4.3 | Non-string `routed_tool` still forces execute-shape | type hole closed |
| R4.4 | `current_step` 1-based wire semantics | explicit anchoring; out-of-range step ⇒ honest text + `step_out_of_range`, no fabricated anchor |
| R4.5 | Hint for a step without an operator | no dangling params clause, zero suggested actions |
| R4.6 | Concept extraction vs polite phrasing | 「请问，什么是大气校正？」/「帮我解释一下NDVI」resolve; unknown terms echo unmangled and stay honest |
| R4.7 | Troubleshoot without observations | asks for exactly one thing (the stats); matched and unmatched answers stay < 8 KiB |
| R4.8 | Seams lazy-load on first tool query | no manual reload needed for grounding to activate |

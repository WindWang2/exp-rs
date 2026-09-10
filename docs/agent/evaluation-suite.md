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

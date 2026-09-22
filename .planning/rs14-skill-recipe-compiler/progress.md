# RS14-20 Progress — LabSpec → ScientificRecipe Compiler

Worktree: `exp-rs-wt-rs14-skill-compiler` · branch `agent/rs14-skill-recipe-compiler` · base `origin/master @ 4f6632e1f`

## Slice ledger

| Slice | Status | Evidence |
|---|---|---|
| A — schema + value object | ✅ green | `test_recipe_schema` 6 cases / 15 assertions; `data/schemas/scientific_recipe.schema.json` (draft-07, closed, versioned `sicnu.scientific_recipe/1`) |
| B — LabSpec input adapter | ✅ green | `test_lab_document` 5/59 + `test_lab_source` 6/23; dual-format (D2 spec_version 1/2 + D3 `sicnu.labspec.v1`); `lab-registry.json` merge for step-less wrappers; repo-relative provenance paths |
| C — stage/checkpoint compiler | ✅ green | `test_recipe_compiler` 8/81; `IOperatorCatalog` seam (`FakeOperatorCatalog` tests / `SidecarOperatorCatalog` union of `algorithm_meta{,/capability}` + `data/agent/capabilities`, `family:*` filtered) |
| D — human-only boundary diagnostics | ✅ green | closed diag vocabulary; `ui_action_step`/`manual_step` info, `unknown_operator`/`param_out_of_range` warnings, `no_steps` error; empty-prompt questions diag'd not dropped |
| E — validator + registry + lookup | ✅ green | `test_recipe_validator` 8/16, `test_recipe_registry` 3/17, `test_recipe_lookup` 6/24; closed vocabularies, depends_on ordering, fail-closed registry, page cap 64, deterministic scoring |
| F — exemplar compilations | ✅ green | **15/15** labs compile clean (`--check` exit 0); artifacts committed under `data/agent/scientific_recipes/` incl. registry-resolved lab12/13/14 + standalone D3 `lab.temporal_analysis` |
| G — equivalence + reuse report | ✅ green | `test_recipe_equivalence` 5/79 (compiled ≡ 5 hand-authored references; committed artifacts byte-equal to fresh compile; mutation tests flip the verdict); `test_teaching_reuse` 5/67; `docs/teaching-reuse-report.md` generated |

## Key decisions taken during implementation

- **Top-level `verifier_hooks`**: grading refs + D3 `expected_results` claims are lab-scoped evidence — emitted as recipe-level hooks (`lab_rules`, `grading_pipeline`, `expected_claim`, `artifact_exists`), same item schema as stage hooks.
- **`unknown_key` is a warning** (lint discipline): the JSON Schema mirrors `additionalProperties:false` for external strictness; the C++ validator is authoritative and warns, matching `lab_spec_loader` lint conventions.
- **`unresolved_registry_source`** wired as a typed `LabDocumentError` reason when a registry `source` fails to load.
- **Provenance paths are repo-relative** (`data/labs/…`) — committed artifacts must be byte-reproducible on any checkout. `source_fingerprint` is fnv1a64 of source bytes (drift detection, non-cryptographic).
- **`wrapper_path`** in `teaching_origin` records the canonical v2 wrapper when content came from a D3 source via `lab-registry.json`.
- **Operator coverage**: union catalog covers all shipped-lab operators including `opencv:gaussian_blur`/`opencv:sobel` (found in `data/agent/capabilities/*.json` arrays — recon's "missing" observation resolved by reading the actual record shape).

## Test totals (all green, run twice where noted)

- 9 recipe test executables: 43 cases / 372 assertions, all passing.
- `ctest` registration verified (PRE_TEST discovery; unrelated `*_NOT_BUILT` sentinels belong to unbuilt targets in this partial build, not this change).

## Build environment

- Configure: `cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr -DFETCHCONTENT_SOURCE_DIR_CATCH2=<build-review catch2-src>` (offline reuse; `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib` for the SDK cmake).
- Parallelism `-j2` throughout.

# RS14-20 Slices — TDD breakdown

Each slice: RED test → minimal impl → refactor → narrow ctest → commit.

## Slice A — recipe schema + value object
- `data/schemas/scientific_recipe.schema.json` (draft-07, closed keys)
- `src/recipes/scientific_recipe.{h,cpp}`: `kSchemaId`, field-name consts, `recipeFromJson`/`recipeToJson` (round-trip), `isScientificRecipe`
- Tests: `test_recipe_schema.cpp` — parse/serialize round-trip, reject wrong schema id, reject missing required fields, deterministic serialization

## Slice B — LabSpec input adapter
- `src/recipes/lab_document.{h,cpp}`: normalized `LabDocument` {format(d2|d3), id, titles, objective(s), prerequisites/assets, steps[{id,title,title_zh,description,operator_id,params,action,teaching_note,completion_hint,detail,headless_note}], expected_artifacts, param_ranges, grading_rules, grading_pipeline, thinking_questions(D2)/questions(D3), glossary terms, principle headings, prerequisite_knowledge, source_path, source_sha256}
- `src/recipes/lab_source.{h,cpp}`: `loadLabDocumentFile(path)` (sniff format: `spec_version` int → D2; `schema=="sicnu.labspec.v1"` → D3), `loadLabDirectory(dir)`, `resolveViaLabRegistry(dir, registryJson)` for step-less wrappers
- Tests: `test_lab_document.cpp` — both formats, unknown format → typed error, missing file, non-object JSON, registry resolution for lab12-shaped wrapper, registry missing entry → diagnostic path

## Slice C — stage/checkpoint compiler
- `src/recipes/recipe_compiler.{h,cpp}` + `provider_interfaces.h` (`IOperatorCatalog`)
- steps→stages, params verbatim, `depends_on` chain, hooks from completion_hint/expected_artifacts/expected_results, preflight (required_operators dedup sorted, required_assets, param_bounds), alternatives from `param_ranges[].values`, evidence (grading_rules ref, pipeline, claims), teaching_origin incl. sha256 of source bytes
- Tests: `test_recipe_compiler.cpp` — happy path per format, operator→stage, action→human_only(ui_action), manual→human_only(manual), questions→reflection, unknown operator → diagnostic `unknown_operator` (fake catalog), param_ranges → bounds+alternatives, artifact path matching `output` param attaches hook to that stage

## Slice D — human-only boundary diagnostics
- `recipe_diagnostics.h` closed vocabulary + severities; every non-compilable element produces a diagnostic; `compilation` block stats
- Tests: lab06-style all-manual lab → recipe with zero operator stages + full diagnostics (nothing silently dropped); `no_steps` lab12 wrapper without registry → error diagnostic, recipe still emitted with teaching_origin; param_out_of_range warning

## Slice E — validator + registry + lookup
- `recipe_validator.{h,cpp}`: schema field presence/types, stage-kind vocabulary, hook kinds, depends_on integrity, id uniqueness, `recipe_id` pattern `lab.<id>`
- `recipe_registry.{h,cpp}`: dir scan `*.json`, fail-closed per-file, `recipeIds()/recipe()/listRecipes()/loadProblems()/status()`; dir policy: `$SICNU_SCIENTIFIC_RECIPES_DIR` → `<cwd>/data/agent/scientific_recipes` → `SICNU_SOURCE_DIR/...`
- `recipe_lookup.{h,cpp}`: `Query{intent?, text?, modality?, required_operators_any?}` → scored sorted hits (deterministic tie-break by recipe_id), top-k cap 64
- Tests: `test_recipe_validator.cpp`, `test_recipe_registry.cpp` (tmp dirs), `test_recipe_lookup.cpp`

## Slice F — exemplar compilations
- Compiler exercised on real repo data via a small `sicnu_recipe_compile` CLI tool (`src/recipes/cli/` or tools/) OR a test-driven generator; commit 6 artifacts:
  lab01, lab02, lab05, lab07, lab11_obia (D2) + lab12_sar_processing (registry→D3) — ≥5 ✓
- Human-authored references for same 5 under `tests/data/scientific_recipes/reference/`
- Tests: validator-clean on committed artifacts; registry loads them

## Slice G — equivalence + reuse report
- `test_recipe_equivalence.cpp`: compiled vs reference — normalized compare (stage kinds sequence, operator sets, intent, asset paths, evidence refs); must fail if a stage is dropped
- `test_teaching_reuse.cpp`: compile all labs in data/labs → aggregate stats emitted as JSON to `tests/data` scratch; then generate committed `docs/teaching-reuse-report.md` (via small reporter in module: `renderReuseReportMarkdown(stats)`)
- Final: targeted regression (`ctest -R "recipe|lab_document|labspec"`), review, rebase-check, PR

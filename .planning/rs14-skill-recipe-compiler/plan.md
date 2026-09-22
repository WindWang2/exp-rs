# RS14-20 Plan — LabSpec → Agent Skill / Recipe Compiler

## Problem statement

Teaching knowledge (`data/labs/*.lab.json`, `*.labspec.json`) and agent knowledge (`data/agent/recipes/`, `pi/knowledge/`) are two hand-maintained silos. An agent cannot consume a lab as a verifiable skill; a lab's checkpoints, parameter pedagogy and grading contract never reach machine consumers. We compile LabSpec documents into **ScientificRecipe** artifacts — versioned, machine-readable skills — with explicit human-only boundaries and diagnostics for anything that cannot compile.

## User stories

- **本科生**: the lab stays the single authored source; compiled recipes make "what counts as done" (verifier hooks, evidence) visible and consistent with the grading contract — clearer scientific-process feedback than clicking buttons.
- **AI Agent**: `ScientificRecipeRegistry::lookup(goal)` returns ranked skill candidates with required assets, stages, preflight, alternatives and evidence requirements — a machine-readable interface, not GUI text.

## Architecture

```
src/recipes/                        (Qt-free; std + jsoncpp only; lib sicnu_recipes)
  lab_document.{h,cpp}              normalized view over .lab.json (v1/v2) + .labspec.json (D3)
  lab_source.{h,cpp}                ILabSource + filesystem/dir/registry-resolving impls
  scientific_recipe.{h,cpp}         recipe value object + (de)serialization, schema const
  recipe_compiler.{h,cpp}           LabToRecipeCompiler: LabDocument → recipe + diagnostics
  recipe_diagnostics.h              typed diagnostic codes (closed vocabulary)
  recipe_validator.{h,cpp}          linter: structural + semantic checks, severities
  recipe_registry.{h,cpp}           dir scan of data/agent/scientific_recipes/*.json
  recipe_lookup.{h,cpp}             semantic lookup adapter (goal keywords/intent/capabilities)
  provider_interfaces.h             IOperatorCatalog (hasOperator/paramNames) minimal seam
data/schemas/scientific_recipe.schema.json
data/agent/scientific_recipes/*.json   compiled exemplars (kind: scientific_recipe)
docs/recipes-integration.md         wiring points for future tracks (planner/verifier/MCP)
docs/teaching-reuse-report.md       generated reuse-rate report
tests/test_recipe_*.cpp             Catch2, link sicnu_recipes + jsoncpp only
```

## ScientificRecipe schema (`schema: "sicnu.scientific_recipe/1"`)

```json
{
  "schema": "sicnu.scientific_recipe/1",
  "recipe_id": "lab.lab02_spectral_analysis",
  "title": "...", "title_zh": "...",
  "goal_pattern": {
    "intent": "spectral_analysis",
    "keywords_en": ["ndvi", "spectral index", "band ratio"],
    "keywords_zh": ["光谱", "植被指数"],
    "modality": "optical"
  },
  "teaching_origin": {
    "kind": "labspec_d2" | "labspec_d3",
    "lab_id": "lab02_spectral_analysis",
    "spec_version": 2,
    "source_path": "data/labs/lab02_spectral_analysis.lab.json",
    "source_sha256": "…",
    "compiled_by": "sicnu.lab2recipe/1",
    "objective": "…", "objective_zh": "…",
    "prerequisite_knowledge": […],
    "glossary_terms": ["NDVI", …],
    "principle_headings": […]
  },
  "required_assets": [
    {"id": "primary", "kind": "raster", "predicate": {"path": "data/samples/…", "must_exist": true}, "note": "…"}
  ],
  "stages": [
    {"id": "s03_calculate_ndvi", "index": 2, "kind": "operator",
     "operator_id": "rs:spectral_index",
     "params": {…verbatim…},
     "title": "…", "title_zh": "…", "teaching_note": "…",
     "verifier_hooks": [
       {"kind": "artifact_exists", "target": "outputs/lab02_ndvi.tif"},
       {"kind": "completion_hint", "text": "…"}
     ]},
    {"id": "s02_observe_spectral_profiles", "kind": "human_only",
     "boundary": "ui_action", "action": "identifyFeatures",
     "reason": "interactive identify tool — no headless equivalent"},
    {"id": "reflection_1", "kind": "reflection", "human_only": true,
     "prompt": "为什么植被 NIR 高、Red 低？"}
  ],
  "preflight": {
    "required_operators": ["rs:spectral_index", "rs:band_math"],
    "required_assets": ["data/samples/landsat_sample.tif"],
    "param_bounds": {"rs:spectral_index": {"red": {"min":1,"max":7}, …}}
  },
  "alternatives": [
    {"stage": "s04_custom_band_ratio", "kind": "param_values",
     "param": "expression", "choices": ["b5 / b4"], "note_zh": "…"}
  ],
  "evidence": {
    "artifacts": [{"path": "outputs/lab02_ndvi.tif", "kind": "raster", "note_zh": "…"}],
    "grading_rules": "data/labs/grading/ndvi_basics.rules.json",
    "grading_pipeline": "data/pipelines/…",
    "thinking_questions": […]
  },
  "compilation": {
    "stages_total": N, "stages_operator": n, "stages_human_only": n,
    "coverage": 0.6, "diagnostics": ["…codes only…"]
  }
}
```

Stage kinds: `operator` (headless-executable), `human_only` (boundary ∈ `ui_action`|`manual`|`reflection`|`judgment`), `reflection` (thinking questions — always human_only). Stages keep source order; `depends_on` = previous stage id (ordered chain; D3 steps are sequential too).

## Compilation rules (deterministic)

| LabSpec element | Recipe output |
|---|---|
| `operator_id` + `params` step | `operator` stage; capability requirement → `preflight.required_operators`; params verbatim |
| `action` step | `human_only` stage, `boundary: "ui_action"`, action name kept |
| manual step | `human_only` stage, `boundary: "manual"` |
| `completion_hint` | `verifier_hooks[].kind="completion_hint"` (soft evidence) |
| `expected_artifacts` (v2) | `evidence.artifacts` + per-stage `artifact_exists` hook when path matches a step `output` param |
| `param_ranges` (v2) | `preflight.param_bounds` + `alternatives[]` for `values` lists |
| `grading_rules` path | `evidence.grading_rules` (reference only) + recipe-level hook `{"kind":"lab_rules","ref":…}` |
| `grading_ref.pipeline` | `evidence.grading_pipeline` |
| `thinking_questions` / D3 `questions` | `reflection` stages (human_only, with `hint` for D3) |
| `prerequisites[]` (data refs) | `required_assets` + `preflight.required_assets` |
| `prerequisite_knowledge` | `teaching_origin.prerequisite_knowledge` |
| D3 `expected_results[]` | `evidence.claims` + `artifact_exists` hooks |
| D3 `operators[]` | `preflight.required_operators` + `operator_roles` metadata |
| D3 `data`, `pipeline` | `required_assets`/`evidence.pipeline` respectively |
| lab12–14 step-less v2 wrapper | resolve D3 source via lab-registry `source`, merge; else `no_executable_steps` diagnostic |

Diagnostics (typed, `{code, severity, stage_id?, field?, message}`): `no_steps`, `unresolved_registry_source`, `unknown_operator`, `param_out_of_range`, `param_not_in_values`, `manual_step`, `ui_action_step`, `missing_asset_path`, `empty_params`, `unrecognized_spec_version`, `duplicate_stage_id`, `invalid_hook`. Severity: `error` (recipe still emitted but flagged `compilation.errors>0` and validator will fail it), `warning`, `info` (human_only markers are `info`).

## Public API sketch

```cpp
namespace sicnu::recipes {
  struct LabDocument { …normalized fields…; Json::Value raw; };
  struct LabSourceError { std::string path, reason; };
  bool loadLabDocument(const std::string &path, LabDocument &out, LabSourceError *err);
  Json::Value compileLabToRecipe(const LabDocument &lab, const IOperatorCatalog &ops,
                                 CompileDiagnostics &diags);   // returns recipe Json
  std::vector<Diagnostic> validateRecipe(const Json::Value &recipe);
  class RecipeRegistry { setDirectory/reload/recipeIds/recipe/listRecipes/loadProblems/status; }
  class RecipeLookup { Json::Value search(const Query&) — scored, bounded top-k; }
}
```

## Migration / compatibility

- Purely additive: new dir, new data namespace, new schema file, one `add_subdirectory` line, one tests block. No existing file semantics change.
- Harness recipes untouched; `recipe_id` namespace `lab.*` disjoint from `harness.*`.
- `.gitignore`: one whitelisting block for `.planning/` (done) — `data/agent/**` already tracked.

## Observability

- `compilation.coverage` + per-recipe diagnostic codes embedded in artifacts; `RecipeRegistry::loadProblems()` typed strings; reuse report aggregates coverage per lab.

## Security / trust boundary

- Compiler parses untrusted JSON → strict type checks, closed key vocabularies, jsoncpp CharReader with defaults (no comments; depth risk noted — inputs are repo-authored lab files, bounded size; keep `stackLimit` default and document).
- Recipes are declarative data; no executable code, no path rewriting — `data/`/`outputs/` params stay relative and are resolved by the future executor under its own policy.

## Performance budget

- Compile is O(steps + params); registry scan O(files); lookup is in-memory scoring over ≤ few hundred recipes, top-k ≤ 64 hard cap, page size cap 64. All offline; no network.

## Test strategy (per slice, TDD)

- Happy path: compile lab02 → assert stages/hooks/evidence/preflight.
- Invalid input: malformed JSON, wrong schema, missing steps, unknown operator (fake catalog), param outside `param_ranges`.
- Boundary: empty steps array, duplicate stage ids, D3 doc without questions, lab with only manual steps.
- Persistence: recipe → JSON → parse → equal (round-trip); registry reload determinism.
- Deterministic replay: compile twice → byte-identical serialization (key order via jsoncpp stable writer).
- Compatibility: v1 lab doc (no v2 fields) still compiles; D3 doc compiles.
- Teaching vs agent consistency: human_only stages carry teaching content verbatim; reflection stages never become operator stages.
- Cancellation/budget: n/a (synchronous pure compile); lookup bounded top-k test.

## Work packages = slices A–G (see slices.md)

## Rollback / kill-switch

- Module is additive; deleting `src/recipes/` + the data dir + two CMake deltas reverts fully. Registry absent → lookup reports typed `unavailable`.

## Definition of Done (track-specific)

1. ≥5 exemplar labs compiled & committed under `data/agent/scientific_recipes/` (mix of D2 + D3 + registry-resolved lab12), each validator-clean.
2. Semantic-equivalence tests: compiled output ≡ human-authored reference recipe for the same 5 labs (normalized comparison).
3. `docs/teaching-reuse-report.md` generated from real compile stats (automatable vs human-only inventory).
4. Machine-readable lookup API (`RecipeLookup::search`) tested for deterministic ranking.
5. All targeted ctest green twice; no new warnings; review ×2.

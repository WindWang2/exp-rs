# ScientificRecipe integration map (RS14-20)

`src/recipes/` compiles LabSpec teaching documents (`data/labs/*.lab.json`,
`*.labspec.json`) into versioned **ScientificRecipe** artifacts
(`schema: "sicnu.scientific_recipe/1"`) under `data/agent/scientific_recipes/`.
This document records the seam map: what this module deliberately does *not*
own, and where future tracks plug in.

## What this module owns

| Component | File | Role |
|---|---|---|
| `LabDocument` | `lab_document.{h,cpp}` | Normalized view over D2 (`spec_version` 1/2) and D3 (`sicnu.labspec.v1`) lab documents. Params stay verbatim. |
| `loadLabDirectory` | `lab_source.{h,cpp}` | Directory scan + `lab-registry.json` resolution for step-less v2 wrappers (lab12–14). Per-file errors never abort the scan. |
| `compileLabToRecipe` | `recipe_compiler.{h,cpp}` | Deterministic pure compile: LabDocument → recipe + typed diagnostics. Never executes anything. |
| `validateRecipe` | `recipe_validator.{h,cpp}` | Authoritative linter: closed key sets, closed vocabularies, `depends_on` acyclicity. |
| `ScientificRecipeRegistry` | `recipe_registry.{h,cpp}` | Directory-backed store of compiled/authored recipes; fail-closed loading, bounded paging. |
| `searchRecipes` | `recipe_lookup.{h,cpp}` | Scored deterministic lookup (intent / modality / keyword / operator facets). The machine-readable surface for agents. |
| `compileDirectoryStats` | `reuse_report.{h,cpp}` | "教学知识复用率" aggregation + deterministic markdown report. |
| `sicnu_recipe_compile` | `recipe_compile_main.cpp` | Offline CLI: compile all labs, emit artifacts, reuse report, stats JSON, `--check` drift gate. |

## What this module does NOT own (single-source-of-truth boundaries)

| Truth | Owner | Recipe layer's use |
|---|---|---|
| Operator existence / semantics / kernels | Processing Registry + `src/operators/` | `IOperatorCatalog` seam — existence checks only (`hasOperator`). `paramNames` is intentionally unimplemented by the sidecar catalog until a live-registry adapter lands. |
| Operator param schemas | `data/processing/algorithm_meta/` sidecars + registry | `SidecarOperatorCatalog` *reads* them; never copies. Unknown operator → `unknown_operator` warning + `operator_known: false`, never a silent fallback. |
| Grading semantics | `data/labs/grading/*.rules.json` (`sicnu.lab.rules/1`) + pipelines | Recipes *reference* rules/pipelines by path (`evidence.grading_rules`, `verifier_hooks[kind=lab_rules|grading_pipeline]`). Never embedded. |
| Lab content | `data/labs/*.lab.json` (canonical), `*.labspec.json` (D3 authoring), `lab-registry.json` | Recipes carry `teaching_origin` provenance + `source_fingerprint` (fnv1a64 — drift detection, not security). |
| Agent plan execution | `src/agent/harness/` (`RecipeCatalog`, `AgentPlan v2`) | Disjoint namespace (`lab.*` vs `harness.*`). ScientificRecipe is declarative data; nothing here schedules runs. |

## Wiring points for future tracks

- **Agent tool surface**: `searchRecipesJson(registry, query)` is the intended
  MCP/agent-tool payload shape (`{hits:[…], total, query}`). A future
  `recipe:*` tool should wrap it; the registry already enforces page caps.
- **Planner (RS14-09-style)**: consume `preflight.required_operators`,
  `preflight.required_assets`, `required_assets[].predicate`, and stage
  `operator_id`/`params` verbatim. `human_only` stages are hard stops, not
  gaps to auto-fill.
- **Verifier / grader (RS14-07-style)**: consume `verifier_hooks` (stage-level
  `artifact_exists` / `completion_hint`; recipe-level `lab_rules` /
  `grading_pipeline` / `expected_claim`). Hooks are descriptors — the
  verifier decides how to evaluate them.
- **Live operator catalog**: implement `IOperatorCatalog` over
  `AtomicAlgorithmRegistry`/processing registry when a Qt-free live handle
  exists; `SidecarOperatorCatalog` remains the offline default.
- **Executor**: *not* this track. A future runner resolves `data/`/`outputs/`
  params under its own sandbox policy; recipes intentionally keep them
  verbatim.

## Determinism / resource bounds

- `serializeRecipe` uses a fixed StreamWriterBuilder (2-space indent, UTF-8);
  jsoncpp sorts object keys → byte-stable artifacts.
- Registry scan is O(files), non-recursive, sorted; lookup is in-memory
  scoring, `limit` hard-capped at `kMaxPageSize = 64`.
- JSON parses cap `stackLimit` at 256 (depth-bomb guard).
- Everything is offline; no network, no Qt, no threads.

## Failure model

Typed diagnostics (`{code, severity, stage_id?, field?, message}`), closed
vocabulary in `recipe_diagnostics.h`. Severity policy:

- `error` — recipe still emitted but `validator` rejects it
  (`no_steps`, schema violations, …).
- `warning` — authored-content drift (`unknown_operator`,
  `param_out_of_range`, `duplicate_stage_id`, unknown keys).
- `info` — intentional human-only boundaries (`ui_action_step`,
  `manual_step`), `empty_params`, dropped empty questions.

Nothing is silently dropped; nothing is silently corrected.

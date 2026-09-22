# RS14-20 Recon — LabSpec → Agent Skill / Recipe Compiler

Baseline: `origin/master` = `4f6632e1f` (post-#1145 union). Fetched 2026-09-22.

## What already exists (do NOT rebuild)

| Asset | Location | Role |
|---|---|---|
| LabSpec v1/v2 loader | `src/app/widgets/lab_spec_loader.{h,cpp}` | Qt-based strict loader; `data/labs/*.lab.json`; rejects v2 keys in v1 docs |
| LabSpec schema | `data/schemas/labspec.schema.json` | normative D2 contract (`spec_version` 1|2) |
| LabSpecCatalog | `src/agent/harness/lab_spec.{h,cpp}` | Qt-coupled raw-JSON catalog for copilot grounding (current-step anchored) |
| D3 labspec format | `data/labs/*.labspec.json` + `data/labs/labspec.schema.json` | `sicnu.labspec.v1`: objectives/principles/data/pipeline/operators/steps/expected_results/questions/glossary |
| Lab registry | `data/labs/lab-registry.json` | `sicnu.lab-registry/1`: canonical id → source `.labspec.json`, aliases, grading_rules, pipeline, data_spec |
| Grading rules | `data/labs/grading/*.rules.json` | `sicnu.lab.rules/1`: assertions[{id,kind,weight,severity,params,derivation}] — existing verifier truth |
| Harness recipes | `data/agent/recipes/*.json` + `src/agent/harness/recipe_catalog.*` | `kind: harness_recipe` — slot/gate plan templates instantiating AgentPlan v2 |
| Capability sidecars | `data/processing/algorithm_meta/capability/rs-*.json` | D8 per-operator capability facts (family, modality, band_roles, determinism, teaching_use) |
| Agent tool surface | `src/agent/harness/recipe_tools.cpp` | `harness:search_recipes|describe_recipe|instantiate_recipe` MCP tools |
| Solutions | `data/agent/solutions/solution.*.json` | authored intent→pipeline answers |

## Lab inventory (master)

- 14 canonical `*.lab.json` (all spec_version 2): lab01–lab11 have inline `steps`; **lab12/13/14 have NO `steps`** — they are v2 metadata wrappers whose executable content lives in the linked `.labspec.json` (via `lab-registry.json` `source`). Note: strict `loadLabSpecsFromDir` flags them (`steps` required, non-empty) — observed pre-existing condition, not ours to fix.
- 4 D3 `.labspec.json`: lab8_temporal, lab9_sar, lab10_hyperspectral, lab11_cartographic (all steps carry `operator_id`).
- Step mix across lab01–11: operator steps (automatable), `action` UI-verb steps (addRasterLayer, identifyFeatures, openComparisonDialog…), manual steps (neither field).

## Gaps this track fills

1. No artifact captures a lab as an **agent-executable skill**: goal pattern, asset predicates, stages with verifier hooks, evidence requirements, human-only boundaries, teaching provenance.
2. No compiler LabSpec→recipe; teaching knowledge and agent knowledge are two hand-maintained sets (labs vs `data/agent/recipes`).
3. Harness recipes lack teaching-origin metadata, param pedagogy (`param_ranges`), grading linkage, and human-only markers — they are plan templates, not skills.
4. No recipe validator/linter, no semantic lookup over teaching-derived recipes.

## Extension seams (stable)

- New Qt-free module `src/recipes/` (namespace `sicnu::recipes`), dep = jsoncpp + C++20 only — mirrors `src/planner/` (#1193) / `src/lab/` (#1197) sibling-track convention.
- Input adapter consumes **raw `Json::Value` lab documents** (both formats normalized to `LabDocument`), so it works with `LabSpecCatalog`, files, or future spec_version-3 `src/lab/` runtime without depending on any of them.
- `IOperatorCatalog` minimal provider iface (`hasOperator`, `paramNames`) — fake in tests; future adapter wires `AtomicAlgorithmRegistry`/`CapabilityCatalog`. Documented in `docs/recipes-integration.md`.
- Output namespace: `data/agent/scientific_recipes/` (kind `scientific_recipe`, ids `lab.<lab_id>`) — separate from `data/agent/recipes/` (`harness.*`); `data/agent/**` already tracked in git.
- Verifier hooks **reference** `sicnu.lab.rules/1` files by path — never inline/copy assertions.

## Explicit non-goals

- No agent execution / no runtime runner for recipes (boundary: "不直接执行 agent").
- No changes to Pi extension (`pi/exp-rs-spatial.ts`) or `pi/knowledge/*`.
- No edits to capability mirror (`data/agent/capabilities`, #1151 territory) or existing RecipeCatalog semantics.
- No GUI panel for recipes.
- Do not fix open issues (#1146–#1187 observed list); lab12–14 strict-loader failures are recorded as observed, not fixed.

## Dedup vs open PRs

| PR | Overlap risk | Resolution |
|---|---|---|
| #1197 labspec2-runtime (`src/lab/`, spec_v3) | LabSpec parsing duplication | We parse JSON docs ourselves into `LabDocument`; spec_v3 is a future input source via the same adapter. No dependency. |
| #1193 task-planner (`src/planner/`) | "plan" vocabulary | ScientificRecipe is a reusable skill artifact, not a plan; planner could consume recipes later via provider iface. |
| #1191 unified-verifier | verifier hooks | We emit hook *descriptors* (`kind`, target, ref) — evaluation belongs to verifiers/graders; no shared code needed. |
| #1190 curriculum-pack | lab organization | We read lab docs directly; curriculum is orthogonal metadata. |
| #1196 process-grader | grading | Grader consumes `sicnu.lab.rules/1`; we only reference those files in evidence. |
| #1189 agent-benchmark | eval | No overlap; our lookup adapter could feed benchmarks later. |

## Risks

- Two lab input formats + registry indirection → adapter complexity; mitigated by normalizing to `LabDocument` early.
- `params` values embed `data/`/`outputs/` paths → keep them verbatim in stages (same resolution rules as lab runtime, documented).
- Schema id conventions differ (`sicnu.X/1` vs `schema_version` int) — we pick `sicnu.scientific_recipe/1` string const, matching lab-registry/rules style.

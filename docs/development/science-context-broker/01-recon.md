# Science Context Broker — Recon (fact-source & wiring matrix)

Baseline tip: `origin/master` @ `a9dc33fa`.
Owns: `src/science_context/**`, thin agent adapters, `tests/test_science_context_*`,
`docs/development/science-context-broker/**`.
Parallel OPEN — never modify: #1237 teaching, #1238 experiment_studio, #1239 teaching_admin.

## What this is / is not

| Is | Is not |
|---|---|
| Single versioned projection `exp.science_context.v1` | Second passport |
| Bounded, deterministic, cacheable bundle | Second recipe registry |
| Skill/capability routing over existing truths | Second planner / IR compiler |
| Shared Agent/Planner/UI seam | Control Center UI |

## Fact-source matrix (authoritative → projection)

| Fact | Authority on this tip | Broker role |
|---|---|---|
| Asset identity / lifecycle | `CatalogRecordStore` / `AssetSnapshot` (`src/data`) | Asset State Provider reads via injected facts / passport |
| File metadata (CRS, bands, nodata) | GDAL `SICNU_*` via `src/scientific_state/gdal` | Projected through passport claims |
| Sensor / modality / product | `SensorProfileFacts` + catalog | Passport `sensor` section |
| Radiometric unit / domain | Passport resolver vocabulary | Capability Router input (DN vs SR) |
| Band roles | Passport `bands[].role` + claims | Missing-role blockers |
| Provenance / derivation | `DerivationRecord` → passport `provenance` | Evidence + open questions |
| Capability knowledge | `data/agent/capabilities/*.json` + `capability_knowledge.*` | Structural candidates (not executable plan) |
| Intent → candidates | `capability_graph.*` (`resolveGoalIntent`, `evaluateFeasibility`, `preparationForWhyNot`) | Capability Router mirrors structural rules; never bypasses planner |
| Scientific recipes | `src/recipes/*` (`ScientificRecipeRegistry`, `searchRecipes`) | Recipe Router: top-K, no auto-exec |
| Harness recipes | `data/agent/recipes/*` + `recipe_catalog.*` | Out of scope (disjoint `harness.*` namespace) |
| Autonomy ladder | `src/agent/autonomy/autonomy_level.*` L0–L5 | Constraints: L2 ⇒ no autonomous exec |
| Planner API | `workflow_planner.*` `CompileWorkflowRequest` / `compileWorkflow` | Planner Adapter fills typed fields only |
| Workbench selection | `agent_context_tool` / `SelectionContext` | Optional asset id seed (not owned here) |

## Wiring seams (append-only)

| Seam | File(s) | Delta policy |
|---|---|---|
| Core library | `src/science_context/**` | New target `sicnu_science_context` |
| CMake root | `CMakeLists.txt` | `add_subdirectory(src/science_context)` after scientific_state |
| Agent tools | `src/agent/data_platform_tools.cpp` | Append defs + prefix + dispatch (same pattern as suitability) |
| Surface parity | `surface_registry` DataPlatform table | Automatic via `dataPlatformToolDefs()` |
| MCP `mcp_server.cpp` | — | **Do not edit**; data-platform dispatch already routes |
| Shared CMake / main_window | — | No edits |
| Parallel tracks | `src/teaching/**`, `experiment_studio/**`, `teaching_admin/**` | Untouched |

## Tool names (conflict check on tip)

| Candidate | Status |
|---|---|
| `data:asset_passport` | Free (integration.md foreshadowed; not registered) |
| `scientific:context` | Free |
| `scientific:capabilities` | Free |
| `recipe:search` | Free (`harness:recipe_*` exist; disjoint) |

## Cache key inputs

`asset_digest` / mtime+size, catalog generation, recipe registry revision,
recipe pack digest, autonomy policy revision → `ContextCache` keys.

## DoD path

`selected catalog asset` → passport (`resolveAssetState`) → ObservedState →
capability query → recipe search → `ScientificContextBundle` →
`PlanningContext` → existing `CompileWorkflowRequest` / `compileWorkflow`;
same bundle via `scientific:context`.

## Tip path corrections vs prompt

Prompt mentioned `src/capability_state_graph/` and `src/planner/` — on this tip
use `src/scientific_state/`, `src/agent/harness/capability_graph.*`,
`src/recipes/`, `src/agent/harness/workflow_planner.*`.

## Baseline

- `master@a89c0c20` (`fix(cli): resume failure carries per-step verdict detail`)
- Branch: `zcode/pi-spatial-scientist-harness-4` (worktree, 3 commits, no master history rewritten)
- Local lane: Windows 11 / MSVC 14.38 / Ninja / shared vcpkg tree; mission compile discipline (`CMAKE_BUILD_PARALLEL_LEVEL=2`; controlled `-j8` for the full build on a 16-core/32 GB box).

## Architecture

Pi remains the **generic agent foundation** (ADR 0122); ExpRS implements the geospatial/remote-sensing harness around it (new **ADR 0130**). Everything new lives under owned directories:

```
src/agent/harness/      error taxonomy | tool taxonomy | tool manifests
                        entity resolver | grounding tools | typed context
                        scientific preflight | AgentPlan v2 + compiler
                        verification | plan runner | recipe catalog
data/agent/recipes/     5 metadata-driven recipes (operators only)
pi/roles/ + pi/         subagent role cards; harness/layout bridge categories
docs/agent/             7 required documents; ADR 0130
```

Invariants kept: single catalog (every callable is an `AgentTool` with a bounded `harness` manifest), single engine (plans compile to `WorkflowDefinition` and run only via `WorkflowRunCoordinator` -> `TaskCenter`; no second scheduler, no LLM-driven concurrency), anti-hallucination by construction (unknown -> inspect, ambiguous -> typed failure with candidates), deterministic science (preflight/verification/recipes/evals are pure code), FAIL never surfaces as success.

## Changes

- **Phase 12 - error taxonomy**: closed stable code table (`DATASET_NOT_FOUND` ... `MAP_PREFLIGHT_FAILED` + harness-internal codes) with category/retry-class/recoverable/suggested actions; legacy codes normalize in.
- **Phases 1/2/14 - canonical catalog**: every catalog entry gains a bounded `harness` manifest (taxonomy `domain.action`, risk class read_only/modifies_display/creates_artifact/modifies_project/destructive/external_process/network, side effects, resource hints, cancellability, preconditions, expected artifacts) derived from `AgentMetadata` + a namespace risk table; `outputSchema` finally reaches the wire.
- **Phases 3/4/19 - grounding & context**: `EntityResolver` (asset-N / governed UUID / path / display name; `ENTITY_AMBIGUOUS` lists candidates), `spatial:understand` typed DatasetUnderstanding with deterministic modality inference (sar/optical/dem), `harness:context` with content-hashed revisions (unchanged -> `{unchanged:true}`).
- **Phase 5 - scientific preflight**: deterministic rule packs (ndvi / change / sar_change / classify / phenology) over inspected facts; `blocked` vetoes execution; unverifiable facts warn, never silently pass.
- **Phases 6/7/8 - plan lifecycle**: AgentPlan v2 (goal/intent/inputs/steps/outputs/verification/map_output; v1 accepted), structural validation with typed issues, single compiler to WorkflowDefinition, per-step + aggregate RAM estimates, `harness:plan` / `harness:execute_plan` / `harness:run_status`.
- **Phases 9/10/11/13 - verification & recovery**: tri-state PASS / PASS_WITH_WARNINGS / FAIL per artifact (existence, openability, CRS, dimensions, finite/NoData fractions, class domain, non-empty, provenance) and per run; FAIL forces status `failed`; final map confirmation hook (layout presence + MapSpec compose/preflight/repair loop); bounded transient auto-resume (engine resumeRun, completed work never re-executed).
- **Phase 16 - recipes**: five metadata recipes (optical vegetation, optical change, SAR change, land cover, phenology) with slot bindings and when_slot/when_param gates that compile to engine DAG dependencies.
- **Phase 17 - Pi subagents**: role cards (data-inspector, planner, scientific-reviewer, cartography-reviewer, result-verifier) with a single-writer rule; reviewers are read-only by construction.
- **Phases 18-20 - evals & budgets**: `tests/test_harness_evals.cpp` (9 deterministic cases; NDVI and optical change execute end-to-end through the real engine on synthetic GeoTIFFs), anti-hallucination typed-failure checks, FAIL-never-success, token budgets with opt-in measurement dump.

## Tests

- New: `test_harness_error` (42), `test_harness_catalog` (93), `test_harness_grounding` (80), `test_harness_evals` (118) - **all green** on the local lane; evals drive the harness tool surface with the real engine and operators (no mocks, no external model).
- Regression batches re-run green: spatial tools/contracts, agent tools 3.0, spatial-scientist benchmark, workspace state, output verifier, workflow engine v2 / coordinator / cancel / recovery / artifact GC / provenance, TaskCenter, execution plane, MapSpec, governance, layout tools, help system, plugin runtime host/loader, golden workflow, fused chain.
- Windows-lane notes: POSIX-fixture targets gated `if(NOT WIN32)` (sockets / fork / SIGSTOP / popen / symlink fixtures) with comments; tests whose *names* contain UTF-8 arrows fail only under ctest name filtering (console codepage artifact; binaries pass directly - pre-existing).

## Performance

- Token budgets measured (`benchmarks/harness-token-budgets.json`, opt-in `SICNU_BENCH_OUT`): typed context 2,411 B (cap 256 KiB), error catalog 1,729 B (cap 8 KiB), 50-tool manifest page 12,501 B (cap 64 KiB); registry cap 512 KiB documented.
- End-to-end eval latency dominated by real operator work on 16x16 fixtures (sub-second per pipeline).

## Compatibility

- Additive only: new `harness:` namespace, new tools, optional `harness` block + `outputSchema` in catalog responses; existing tool names, MCP meta tools, and schemas unchanged. Plan schema versioned (v1 accepted, v2 emitted). Recipes are new tracked config.
- Cross-platform repairs touch shared files but are compile-enabling fixes for pre-existing master defects (details below) - semantics preserved.

## Risks

- Auto-resume transient classification is message-heuristic; bounded to one attempt and the engine re-validates outputs before skipping work.
- MCP workflow-run outputs are not yet governed assets (pre-existing TODO(P1-E1)); run verification therefore warns (not fails) on missing provenance sidecars.
- 3.0-era static-guarded tool registrations drop tools on `SpatialToolRegistry::reset()` (harness tools are reset-safe; 3.0 fix deferred as hot-path).

## Cross-platform repairs included (pre-existing master defects)

- `class`/`struct` forward-declaration mismatches MSVC mangles: `AlgorithmTaskInfo` (`workflow_run_coordinator.h`), `AssetSnapshot` (`interaction_tool_registry.h`, `workspace_service.h`).
- Plugin SDK POSIX surface: new `src/sdk/exprs/msvc_posix_shim.h` (dirent/dlfcn/lstat/chmod/mkdir) wired into discovery/package/loader/registry + `external_tool_operator.cpp`; `S_ISREG` fallback.
- `qgsproject.cpp` POSIX directory fsync guarded `#ifndef Q_OS_WIN`; `gmtime_r` -> `gmtime_s`; QScintilla stub `staticMetaObject` definitions; incomplete-type `unique_ptr<QgsRasterShader>` member.

## Documentation

- New: `docs/agent/spatial-scientist-architecture.md`, `tool-contracts.md`, `scientific-preflight.md`, `result-verification.md`, `evaluation-suite.md`, `workflow-integration.md`, `pi-adapter.md`; `docs/adr/0130-harness-4.md`; `pi/roles/README.md`.
- Updated: `CHANGELOG.md`, `CONTEXT.md` (terms + ADR index), `PROJECT.md` (long-term capabilities), `pi/knowledge/`, `.planning/pi-spatial-scientist-harness-4/` (GOAL / BASELINE with 12 traces + 20-gap register / ARCHITECTURE / PLAN / TEST_MATRIX / REVIEW_LOG (6 adversarial reviews, P0=0 P1=0) / DOCS_LEDGER / FINAL_REPORT).

## Known limitations

- Scientific validity is enforced only for the shipped rule packs; unknown intents degrade to shared structural rules (documented).
- Provenance sidecars for workflow-run outputs are warning-class until the OutputCommitter registration lands on the MCP workflow path.
- Registry-seam enforcement of the 512 KiB tool-output cap deferred (harness tools measured far below; caps asserted by evals).

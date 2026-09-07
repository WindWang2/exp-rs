# GOAL — Pi Spatial Scientist & Agent Harness 4.0

Epic branch: `zcode/pi-spatial-scientist-harness-4`
Worktree: `C:\Users\wangj.KEVIN\projects\exp-rs-pi-harness-4`
Baseline: `master@a89c0c20`

## Mission

Based on the existing Pi integration (ADR 0122) and the Spatial Scientist 3.0
contract layer (ADR 0127/0128), close the remaining harness gaps so that Pi can
reliably **inspect data, plan remote-sensing pipelines, invoke the right tools,
execute, verify, and explain — without ever guessing datasets, bands, CRS,
results, or success**.

```
Pi (generic agent loop)
  → ExpRS Spatial Harness      (this epic)
  → Tool Discovery / Typed Spatial Context
  → Scientific Preflight
  → Plan / Workflow
  → RSOperator / Data / Model Runtime / MapSpec
  → Structured Artifacts
  → Verification
  → Scientific Answer / Map / Dataset
```

## Architecture invariants (non-negotiable)

1. **Pi stays generic**: no conversation engine, generic planner, subagent
   framework, generic memory, or generic tool protocol is copied into C++.
   ExpRS owns spatial context, dataset grounding, operator schemas, workflow
   execution, scientific constraints, artifact semantics, map semantics,
   verification, and provenance.
2. **Single authoritative execution engine**: agent plans compile to
   `WorkflowDefinition` and run through `WorkflowRunCoordinator` →
   `TaskCenter` (ADR 0123). No second workflow engine.
3. **Single tool catalog**: everything Pi can call is one entry in
   `AgentToolCatalog` via a `ToolProvider`; execution stays with the owning
   subsystem (SpatialToolRegistry inline, ToolCallDispatcher→TaskCenter async).
4. **Anti-hallucination by construction**: `unknown → inspect`,
   `ambiguous → resolve`, `missing → typed failure`. Entity ids resolve against
   authoritative registries only.
5. **Deterministic core**: preflight, verification, recipes, and evals are
   deterministic C++/JSON — never LLM judgment.
6. **Minimal footprint in hot files** (root CMakeLists, PROJECT.md,
   CONTEXT.md, CHANGELOG.md, main.cpp, global registries): other epics are in
   flight; integration edits are additive and small.

## Directory ownership

Primary: `src/agent/harness/**` (new), `src/agent/spatial_tools/**`,
`src/agent/contracts/**`, `src/agent/tool_catalog/**`, `src/agent/mcp_server*`,
`pi/**`, `data/agent/**` (new), `docs/agent/**`, `tests/test_harness*`,
`.planning/pi-spatial-scientist-harness-4/**`.

Touch only through public contracts: `processing/`, `runtime/`, `sdk/`,
`app/`, `gui/`, `data/`, `cartography/`. The only exceptions (thin seams):
`src/agent/mcp_server.cpp` (namespace + verification handler wiring),
`src/agent/CMakeLists.txt`, `tests/CMakeLists.txt` (new targets appended).

## Milestones

| # | Milestone | Goal phases | Status |
|---|-----------|-------------|--------|
| M0 | Worktree + branch + build | — | DONE |
| M1 | Phase 0 deep audit, 12 workflow traces, gap register | 0 | DONE |
| M2 | Harness spine: error taxonomy + tool manifest + taxonomy (Phases 12/1/2) | 1,2,12 | |
| M3 | Typed context + dataset grounding (Phases 3/4) | 3,4 | |
| M4 | Scientific preflight + plan compile + resource-aware execution (Phases 5–8) | 5,6,7,8 | |
| M5 | Verification + map confirmation + structured results + retry + safety (Phases 9–11, 13, 14) | 9,10,11,13,14 | |
| M6 | Long-running workflow exposure + recipes + Pi subagents (Phases 15–17) | 15,16,17 | |
| M7 | Evals + anti-hallucination tests + token benchmarks (Phases 18–20) | 18,19,20 | |
| M8 | 6× adversarial review + docs + FINAL_REPORT + PR | — | |

## Acceptance (from the mission)

Pi keeps generic foundation · typed spatial tool catalog · real metadata
grounding · deterministic scientific preflight · plans converge to the
authoritative Workflow/RSOperator stack · resource-aware execution · automatic
verification · final-map confirmation · structured results/errors · real
long-running state · no false success after FAIL verification · RS workflow
evals · token-efficient discovery · docs synced · local tests green · review
P0/P1 = 0 · PR submitted.

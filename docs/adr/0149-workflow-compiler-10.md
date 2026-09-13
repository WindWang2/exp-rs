# ADR 0149: Scientific Workflow Compiler 10.0 — Typed WorkflowIR, Static Analysis & Contract-Driven Repair

Status: accepted · Branch `zcode/scientific-agent-workflow-compiler-10` ·
Baseline `origin/master@7d78059d1a` · Owner: agent/harness

## Context

The harness (ADRs 0130-harness, 0142, 0144-harness-8, 0145, 0146) owns a closed
intent vocabulary, machine-decidable capability knowledge over all 111 `rs:`
operators, a relation graph with deterministic chain composition, AgentPlan v2 as
the single plan→engine bridge, evidence sidecars, and a bounded diagnose tool.
What it still lacks is a COMPILER: the agent-authored plan document carries its
science in schemaless step JSON, cross-step scientific contracts (CRS, grid,
wavelengths, numeric domain, temporal alignment, model inputs) are checked only
inside intent-keyed preflight rule packs, and the only automatic repair is the
relation graph's grid fixer. Everything else — reprojection, calibration, band
selection — is prose advice the LLM may or may not follow. Long sessions have no
harness-side checkpoint, and the Pi TS bridge still forks its transport.

## Decision

1. **WorkflowIR 1.0** (`src/agent/harness/workflow_ir.*`): a versioned, bounded,
   serializable typed document (`kind: "workflow_ir"`) whose nodes carry operator
   ids, typed ports, and artifact facts — kind, CRS, grid, band roles,
   wavelengths, numeric domain, temporal domain, modality/sensor, determinism
   grade, resource/device estimates, provenance expectation, and a user-visible
   semantic output. Every fact carries `fact_status` provenance
   (observed/declared/derived/assumed). The reader is fail-closed; normalize is
   deterministic and idempotent; the fingerprint mirrors `planFingerprint`.
2. **The compiler never executes and never opens data.** Facts enter through the
   existing grounding seam (`resolveDatasetRef` → `DatasetUnderstanding`), the
   existing capability knowledge/catalog/relation layers, and model contracts.
   Lowering goes IR → AgentPlan v2 → the existing
   `compilePlanToWorkflowJson`. One bridge in, one bridge out; no second scheduler.
3. **Static analysis** (`workflow_analysis.*`): 18 closed check families over the
   normalized IR + resolved facts, each a pure function; typed issues reusing the
   stable error taxonomy (6 codes appended additively: WAVELENGTH_INCOMPATIBLE,
   TEMPORAL_MISALIGNMENT, CATEGORICAL_MISMATCH, RESOURCE_OVER_BUDGET,
   OUTPUT_PATH_COLLISION, NONDETERMINISTIC_CHAIN, FACT_CONFLICT). Deterministic
   issue order. Unknown facts degrade checks to warnings — never fake pass/fail.
4. **Contract-driven repair insertion** (`workflow_repair.*`): a closed rule table
   keyed by issue code; each row names the existing operator it inserts, the facts
   it requires, and a risk class. `shape_preserving` repairs (reproject/align/
   resample/band-extract) auto-insert with an evidence record; `radiometric`
   repairs (calibration) auto-insert ONLY from observed metadata; `science_changing`
   repairs (QA mask, gap fill, temporal normalize) NEVER auto-insert — they become
   typed `decision_required` refusals. Every insertion changes the IR fingerprint
   and is recorded (`repairs[]`, `refusals[]`); silent science changes are
   structurally impossible.
5. **Staged planner** (`workflow_planner.*`, `harness:compile_workflow`): intent →
   grounding → candidates → IR → analysis → repair → lower → plan, each stage
   reported with status; alternatives carry why/why-not and deterministic ranking;
   missing facts and limitations are first-class outputs.
6. **Harness session checkpoint** (`context_checkpoint.*`,
   `harness:workflow_session`): file-backed sessions under
   `~/.rs_studio/harness_sessions/` (env override) storing the normalized IR,
   analysis, repair records, decisions, failed attempts, and fact identities;
   resume invalidates stale facts by stat identity and rewinds the stage cursor —
   prior decisions and attempt history survive.
7. **Repair-loop guard**: `harness:diagnose_run` gains a typed per-run attempt
   ledger with error fingerprints; a repeated fingerprint stops the loop
   (`REPEATED_ERROR`) instead of letting the caller re-issue the same failed
   repair. The loop stays Pi-driven (ADR 0145 decision 7).
8. **Knowledge budgeting** (`tool_shortlist.*`, `harness:tool_shortlist`): a
   deterministic, provenance-carrying shortlist of tools/capabilities under a hard
   8 KiB page budget, so prompts never need all 111 operators.
9. **Pi bridge single-implementation + drift guard**: `exp-rs-spatial.ts` imports
   the shared `McpBridge` from `./mcp_bridge.js`; the line-buffer overflow kills
   the child (lazy-respawn) instead of zombie-streaming (F-PI-1); the startup
   deadline is cancelled on every settle path in both files (F-PI-2); a node test
   (`pi/test/no_drift.test.mjs`) fails if a second bridge implementation reappears.

## Non-goals

No operator kernels, no new capability sidecars, no engine changes, no new agent
loop, no help-store work, no CI. Algorithms stay in their tracks; the compiler
consumes their declared contracts.

## Consequences

Agent surfaces can accept typed IR documents and get compile-time scientific
diagnostics before any execution; repairs and refusals become evidence instead of
prose; sessions survive process restarts. Six new error codes join the taxonomy
(additive; drift tests extended). The eval corpus gains compiler categories. The
TS bridge loses its fork; the two P2 bridge findings close.

# GOAL — D17 · Node-based Visual Workflow Pipeline Designer & Agent IR Compiler

Deliver the D17 epic on branch `zcode/workflow-pipeline-designer` (worktree
`../exp-rs-workflow-pipeline-designer`), 100% local, no remote CI:

1. **Workflow IR 2.0** — Qt-native typed pipeline document (`PortFact` /
   `NodeFact` / `EdgeFact` / `WorkflowDefinition`) with lossless bidirectional
   JSON mapping (`S(D(J)) ≡ J`, `D(S(A)) ≡ A`) and a V1 → V2 migration path.
2. **DAG engine** — Kahn in-degree concurrency tiers + DFS 3-color back-edge
   cycle detection with closed cycle-path extraction.
3. **Contract checker & repair engine** — closed-form adapter injection
   (`rs:reproject`, `rs:resample`, `rs:radiometric_calibration`,
   `rs:atmospheric_correction`) with the repair invariant
   `inspect(apply(infer(W))) ≡ ∅`.
4. **Plan optimizer & cost estimator** — SHA-256 lineage signatures, DNE, CSE,
   Flops/Peak-RSS cost model with the 0.70 waterline degradation advice.
5. **Pipeline run coordinator** — tier-scheduled async execution, skip
   cascade, two-phase atomic checkpoints (tmp → fsync → rename), resume with
   cache-hit reuse, cancel.
6. **Qt 6 node-graph canvas** — cubic Bézier connections with the analytical
   midpoint property, 12 px port snapping, zoom clamp [0.2, 3.0], 100-node
   headless load budget.
7. **Guided workbench** — LabSpec step cards ⇄ topology two-way projection
   over a single source of truth with a reentrancy guard.
8. **Agent orchestrator tool** — deterministic NL-goal → DAG compilation for
   the 5 classic intents + execution-error-pattern self-healing loop.
9. **E2E suite** — 100-node / 10-tier scale run under the RSS budget, crash
   consistency (kill at ~50% → resume, completed nodes reused), and all 11
   shipped `data/labs/*.lab.json` templates executed green headless.

## Hard gates (from the D17 brief)

- `ninja -j2` max build; `ctest -j1`; `QT_QPA_PLATFORM=offscreen`.
- ≤ 3 read-only subagents, no recursive spawning.
- Spec-first (ADR + PLAN + DECISIONS before production code), Matt-Pocock
  vertical slices (test → minimal impl → commit), dual-axis review with
  P0 = P1 = 0.
- Master workspace strictly read-only; all work in the D17 worktree.
- Independent ground truth in every test (analytical graph solutions, hand
  computed expectations); no tautologies, no private access, no horizontal
  bulk slicing.

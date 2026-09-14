# DECISIONS — scientific-agent-workflow-compiler-10

| # | Decision | Why | Alternatives rejected |
|---|---|---|---|
| D-001 | WorkflowIR is a typed layer ABOVE AgentPlan v2; lowering goes through the existing `compilePlanToWorkflowJson` | ADR 0130-harness: one plan→engine bridge; ADR 0145: no second scheduler | Replacing AgentPlan (breaks recipes/tools/tests); parallel compile path (duplication) |
| D-002 | IR is agent-authorable wire document (kind `workflow_ir`, schema 1.0), not a hidden C++ struct only | 专项验收: Agent 输出 typed WorkflowIR; versioned+serializable is a goal requirement | Internal-only IR (not serializable, fails goal B) |
| D-003 | Compiler never opens datasets; facts come from resolved DatasetUnderstanding docs | grounding/entity_resolver are the authorities; compiler stays pure & testable | Re-probing files in analysis (second I/O path, untestable) |
| D-004 | Repair risk classes: shape_preserving / radiometric / science_changing; only first two can auto-insert, radiometric only with observed metadata | goal D: 不 silent auto-fix; harness-9 preparation-table precedent (only deterministic transforms auto-apply) | Auto-insert everything (silent science changes); refuse everything (goal D demands insertion when facts suffice) |
| D-005 | New error codes appended to the closed taxonomy (additive, with category+retry mapping) | taxonomy is the one vocabulary; appending is the sanctioned extension style | Separate compiler code namespace (splits the vocabulary) |
| D-006 | `qa`/`gap_fill`/`temporal normalize` repairs are decision-required, never auto | science-changing; F-OPS-3 qa_mask fail-open still open upstream | Auto-insert with warning (silent semantics change) |
| D-007 | Context checkpoint persisted under `~/.rs_studio/harness_sessions/` + env override | sibling convention to engine `~/.rs_studio/checkpoints` (workflow_checkpoint.cpp:44) | Project-dir writes (surfaces files in user data); DataManager extension (out of scope) |
| D-008 | Repair loop stays Pi-driven; typed attempt ledger + REPEATED_ERROR guard inside diagnose_run | ADR 0145 decision 7 (no second agent loop) | Autonomous C++ retry loop (violates architecture) |
| D-009 | pi de-dup: `exp-rs-spatial.ts` imports shared `McpBridge` from `mcp_bridge.ts`; drift test guards against re-forking | review F-PI-2 recommended fix; kills the two-implementation class | Keep two files + assert equality by tests (drift returns) |
| D-010 | ADR number 0149 (temporal track claimed 0148 on its branch) | first-come ADR ledger; filenames disambiguate | 0148 (collision with #973) |
| D-011 | New eval categories in NEW case files; existing 17 files untouched | append-only shared-data policy (OWNERSHIP.md) | Editing existing category files (merge conflicts) |
| D-012 | Shortlist budget 8 KiB, manifest 64 KiB, error catalog 8 KiB (existing), checkpoint doc ≤ 64 KiB | budget pattern precedent (capability_catalog.h:133) | Token-count-only budgets (not measurable locally without tokenizer) |

# Autonomy defaults ledger
- Format/source authorities: CapabilityKnowledge/Catalog/Relations + DatasetUnderstanding; IR schema version "1.0".
- Failure handling: fail-closed per entry (loadProblems), never abort the scan.
- Naming: `workflow_ir/analysis/repair/planner`, `context_checkpoint`, `tool_shortlist`; tests `test_workflow_ir|analysis|repair|planner`, `test_context_checkpoint`, `test_tool_shortlist`; ADR `0149-workflow-compiler-10.md`.
- Timeouts: single build command ≤ 10 min guard; targeted tests serial.
- Outward actions: fetch/push -u/gh pr create only.
- Out-of-scope findings: EVIDENCE.md OUT_OF_SCOPE (F-OPS-3/4 recorded as design constraints).
- No new third-party dependencies.

# CAPABILITY MATRIX — harness 10.0 baseline → target

| Capability | Baseline (master 7d78059d) | Target (this track) |
|---|---|---|
| Typed workflow document | AgentPlan v2 (steps schemaless JSON) | WorkflowIR 1.0 typed nodes/ports/artifact facts + lowering to AgentPlan |
| Pre-execution scientific checks | intent-keyed preflight rule packs (5+ intents; shared rules) | 18-family intent-agnostic static analysis over typed facts (any plan incl. custom) |
| Wavelength checks | band-role presence only | center/min/max windows vs capability demands |
| Numeric-domain checks | radiometric_state acceptable/warn lists | reflectance/dn/db/linear_power/categorical/index domain graph w/ calibration-gated repairs |
| Auto repair | composeChain grid fixer (rs:align) only; preparation TABLE (suggestions) | closed rule table w/ risk classes, evidence records, refusals |
| Planner stages | tools return pieces (resolve_intent/recipes/compose_chain) | one staged pipeline w/ per-stage status + alternatives + ranking |
| Long-task context | ContextLedger in-memory + engine run checkpoints | file-backed session checkpoint/resume/compaction/staleness |
| Repair loop | diagnose_run one-shot proposals + budget | + typed attempt ledger, repeated-error stop, intent echo |
| Knowledge budgeting | manifest 64 KiB, error catalog 8 KiB, run summaries 8k tokens | + tool shortlist 8 KiB w/ provenance + budget report |
| Evidence | provenance/uncertainty/verification sidecars + explain 2.0 | + IR projection (repairs, fact provenance, determinism verdict) |
| Pi bridge | two diverging TS implementations; F-PI-1/2 open | single shared bridge + drift test + both P2s fixed |
| Eval corpus | 17 categories / 91 cases | + 3 compiler categories (compiler/repair-refusal/session) |

# Gap closure map (goal §四 → deliverable)
A→BASELINE.md inventory + this matrix; B→workflow_ir; C→workflow_analysis; D→workflow_repair;
E→workflow_planner; F→context_checkpoint; G→run_loop+ledger; H→tool_shortlist; I→explain IR
projection; J→pi dedup+drift test; K→new eval categories.

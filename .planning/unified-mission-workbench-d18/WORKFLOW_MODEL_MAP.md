# WORKFLOW_MODEL_MAP

See `AUDIT_WORKFLOW.md` for the full graph. Short form:

```text
NL / LabSpec / recipe
  → WorkflowIR 1.0 (agent)
      → AgentPlan v2
          → Engine 2.0 JSON → WorkflowRunCoordinator → TaskCenter
      → migrateFromV1 → IR 2.0 document
                            → canvas / GuidedWorkflow (D17)
                            → PipelineRunCoordinator (designer runs)
```

Compatibility projections to test later: IR1→IR2 round semantics; AgentPlan→Engine2; IR2 optimize/repair idempotence (already D17 tests).

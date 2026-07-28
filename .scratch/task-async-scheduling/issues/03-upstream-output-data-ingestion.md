# 03 — Dynamic Upstream Output Data Ingestion

**Type:** wayfinder:grilling

## Question

When an upstream parent task completes, how should its generated output dataset path automatically be substituted into downstream child task parameter maps before execution commences?

## Blocked by

02 — Task Pipeline DAG Representation & Dependency Gating

**Status:** closed

### Resolution
- Supported `${task.<parent_id>.output}` template placeholders in child task parameter maps.
- `TaskCenter` substitutes placeholders with the parent task's `outputLayerPath` upon parent completion.
- Documented in [0004-upstream-output-placeholder-substitution.md](../../docs/adr/0004-upstream-output-placeholder-substitution.md).

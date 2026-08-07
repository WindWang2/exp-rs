# ADR 0083: Reusable Optical Preprocessing DAG

## Context

The preprocessing steps (calibration, QA masking, atmospheric correction,
indices) were individually reachable, but the mission's B8 wants a coherent
Preprocessing Workbench that guides a sequence instead of making users
discover unrelated dialogs, and E1 wants the sequence reusable as a DAG.
The app already has a WorkflowRuntime (ADR 0029–0031) with builtin
definitions and `$stepId.artifact` parameter flow (ADR 0016); the workbench
should be expressed as a definition over the existing operators, not a new
engine or duplicated algorithms.

## Decision

1. **`lab.preprocess.optical`** (`builtin_definitions.cpp`): a guided
   TaskPanel-hosted workflow chaining the existing `rs:` operators —
   `rs:radiometric_calibration` (TOA reflectance) → parallel `rs:qa_mask`
   (cloud_and_shadow) and `rs:atmospheric_correction` (DOS1) →
   `rs:spectral_index` (NDVI) — with `$stepId.artifact` placeholders flowing
   outputs into downstream inputs (ADR 0016/0031). The NDVI step omits band
   numbers so the operator resolves them from the product's semantic band
   roles (ADR 0065). The QA mask is a side artifact (applying it to the
   corrected product awaits an apply-mask operator).

2. **`tool.rs.qa_mask`** atomic tool registered for the QA-mask step to be
   reachable in the Task Panel and Pipeline editor.

## Consequences

- The analysis-ready sequence is one reusable definition, executable through
  the same WorkflowRuntime / Task Center path as every other workflow —
  no second workflow engine, no duplicated algorithms.
- The definition's structure (operators, artifact flow, role-resolved NDVI)
  is pinned by a workflow-runtime test; the builtin registry test tracks the
  definition count.
- A full visual workbench surface (B8 polish) can build on this definition
  via the existing Pipeline editor.

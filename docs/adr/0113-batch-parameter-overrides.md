# ADR 0113: Batch-Processing Parameter Overrides for RS Operators

## Context

Slice 52 (ADR 0110) made single-input RS operators batchable, but they ran
with their declared schema defaults only — a user could not change
`rs:qa_mask`'s `source`/`mask` selection or `rs:pca`/`rs:mnf`'s component
count before batch-running over many files. The batch dialog offered no
parameter editing at all.

## Decision

- The batch dialog gains an **"RS 参数"** section, rebuilt whenever an RS
  operator is selected (`rebuildParamForm`): one editor per schema parameter
  (excluding the main input and `output*` ports), typed from the
  `AlgorithmDescriptor` (`Enum` → combo, `Boolean` → checkbox, `Integer` →
  spin, `Numeric` → double spin, other → line edit), prefilled from declared
  defaults, widget names `rsParam_<name>` for tests/automation. The section
  hides for QGIS algorithms (which keep their registry-driven parameter UI).
- `collectParamOverrides()` reads the form into a `QJsonObject`;
  `runBatchItem()` gained an optional overrides argument merged over the
  declared defaults. The main input and the output path are **always** derived
  from the batch item — an override cannot hijack them.
- `collectParamOverrides()` is public so tests (and programmatic callers) can
  verify the form state without driving `onRun`'s modal dialogs.

## Consequences

- Batch workflows can now tune the operator (QA source/class rules, component
  counts, thresholds) once for an entire file list instead of only running
  defaults; the QGIS-algorithm path is untouched.
- Pinned by two `test_batch_processing_dialog` cases: the form surfaces schema
  defaults and reflects edits; overrides reach the operator
  (`generic_bitmask` + `bits` + explicit `qa_band` produces the mask) while an
  `input` override is ignored (batch item wins).

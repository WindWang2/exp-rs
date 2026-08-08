# ADR 0108: Operator Result Summary in Dialogs (`runOperatorTask` + onResult)

## Context

The DoD lineage ends derived-product stages with "quality metrics and
statistics", and the operators already return them (post-classification
transition matrix + changed percent, QA-mask masked percent, ...) — but the
dialogs discarded the result JSON: `RasterProcessingDialogBase::runOperatorTask`
completed with only the output path, so the metrics never reached the UI.

## Decision

- `RasterProcessingDialogBase::runOperatorTask` gains an overload taking an
  optional `onResult(std::function<void(const Json::Value &)>)` callback,
  invoked on the GUI thread after the standard completion handling (the
  existing two-argument form delegates with an empty callback — no behavior
  change for the other dialogs).
- `PostClassificationDialog` uses it: a summary label renders the operator's
  changed-pixel count/percent and the non-zero transition-matrix cells
  (行=前时相，列=后时相) right in the dialog after the run.

## Consequences

- Quality metrics from the shared operator results are now visible in the
  task-centric UI without duplicating the computation; other dialogs (QA
  mask, change detection) can adopt the same hook cheaply.
- The wiring seam is pinned by a base-class test: a no-op operator's result
  JSON is delivered through `runOperatorTask`'s callback end-to-end
  (27 assertions / 3 cases green), and the post-classification dialog suite
  stays green (42/1).

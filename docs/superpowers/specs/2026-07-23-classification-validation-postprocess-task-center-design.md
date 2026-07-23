# Classification Validation and Post-processing Task Center Design

**Issue:** #29 — Migrate Classification validation and post-processing to Task Center

## Goal

Route Classification cross-validation and every post-processing run through one Task Center-owned Algorithm Task while preserving current Classification-visible outcomes.

## Scope

- Cross-validation submitted by `QgsClassificationMainWindow::runCrossValidation()`.
- Post-processing submitted by `QgsClassificationMainWindow::runPostProcess()`.
- Task Center terminal state, cancellation, result payloads, session layers, workflow state, status messages, and existing CV result dialog.

Out of scope:

- Classification apply and preview, completed in #28.
- New post-processing algorithms, CV algorithms, or UI features.
- Changes to the existing `RsPostProcessTask` and `RsCvTask` computation semantics.

## Architecture

`QgsClassificationMainWindow` remains the UI owner and its existing
`onClassificationTaskUpdated(const sicnu::AlgorithmTaskInfo&)` remains the
single terminal-event consumer. Its pending-run state is generalized from the
apply/preview boolean to an operation type with four values: apply, preview,
post-process, and cross-validation.

Both #29 operations create their existing worker (`RsPostProcessTask` or
`RsCvTask`) and submit its executor plus cancellation hook through
`TaskCenter::submitJob`. The worker is kept alive in the pending-run state
until the matching Task Center terminal update arrives. The window then clears
busy state, applies the operation-specific existing UI outcome, and deletes the
worker exactly once.

JobEngine remains an implementation detail of Task Center. Classification UI
does not call `RsJobRunner` for these two operations.

## Data Flow

1. The user starts post-processing or cross-validation in the Classification window.
2. The window creates its current `JobRequest` and worker task.
3. `TaskCenter::submitJob(request, executor, cancelHook)` creates one
   `AlgorithmTaskInfo` and starts the underlying worker.
4. Task Center publishes queued, running, progress/log, and terminal updates.
5. On the matching terminal update, the window reads `TaskStatus`,
   `errorMessage`, and the worker result, then restores the existing visible
   outcome for that operation.
6. For cancellation, Task Center publishes Canceled only after JobEngine has
   confirmed the worker has exited; the worker is never deleted while running.

## Terminal Outcomes

### Post-processing

- **Completed:** mark workflow post-processing result, remember raster/vector
  outputs, optionally load valid output layers into the session map, refresh
  workflow UI, and show the existing completion message.
- **Failed:** preserve current output/session state, log the failure, and show
  the existing failure message using worker error first and Task Center error
  as fallback.
- **Canceled:** preserve current output/session state and show the existing
  cancellation message.

### Cross-validation

- **Completed:** show the existing 5-fold accuracy summary dialog and
  completion status message.
- **Failed:** preserve current UI state and show the existing error message.
- **Canceled:** preserve current UI state and show the existing cancellation
  message.

## Test Seams

The approved public seams are:

1. **Task Center:** submit real Classification post-processing and
   cross-validation worker executors through `TaskCenter`, then observe task
   status, result payload, failure error, cancellation, and post-processing
   output files.
2. **Classification window:** compile and exercise its existing test-visible
   construction interface while the migrated implementation consumes Task
   Center terminal updates for user-visible outcomes.

Tests must not call private window methods or inspect source text. Each test is
a vertical TDD slice: write a failing behavior test, verify the expected red
failure, add only the required production change, and rerun it green before
starting the next slice.

## Acceptance Mapping

| Issue criterion | Design response |
| --- | --- |
| CV enters Task Center as one task | CV submits one callable JobRequest through Task Center. |
| Each post-process run enters Task Center as one task | Post-processing submits one callable JobRequest through Task Center. |
| Visible outcomes unchanged | Operation-specific terminal handling preserves existing layers, workflow state, dialogs, and messages. |
| Success, failure, cancellation tested | Real worker executors are observed through the approved Task Center seam. |

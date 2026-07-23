# Task 3 report: Classification post-process and CV Task Center migration

## Scope and APIs

Changed only the Classification window implementation/header, its window-level
tests, and this report. The existing UI helpers now route through these public
production APIs:

- `startPostProcessTask(const RsPostProcessConfig &, bool, const QString &,
  const QString &)`, returning the Task Center ID or `-1` while the
  Classification window is busy;
- `startCrossValidationTask(const cv::Mat &, const cv::Mat &,
  RsCvTask::ClassifierFactory)`, with the same return contract.

The pending state is now a `PendingClassificationOperation` enum covering
Apply, Preview, PostProcess, and CrossValidation. It keeps a generic
`QgsTask *` worker plus the post-process layer-loading option. The existing
Task Center `taskUpdated` listener performs all terminal UI behavior and calls
`deleteLater()` only after it receives the matching terminal Task Center
update.

`runPostProcess()` and `runCrossValidation()` retain their UI preparation and
now call the public start APIs. The APIs submit the existing workers with
`TaskCenter::submitJob`, preserve the `module:classify:postprocess` and
`module:classify:cv` IDs (or the supplied post-process algorithm ID), forward
their cancellation hooks, and preserve the prior payload keys, layers,
workflow state, status text, failure/cancellation reporting, and CV result
dialog.

## RED evidence

Added two `[classify][task_center]` window-level tests before the public APIs
existed. The post-process test creates a 32x32 temporary class-ID GeoTIFF,
starts the window API, checks Task Center metadata, waits for completion, and
checks the output file. The CV test supplies known-good Gaussian data, checks
the Task Center metadata, uses a zero-delay timer to close the result dialog,
and confirms the window remains usable.

The initial requested build failed because neither public production method
existed:

```text
$ cmake --build build-task-center-26 --target test_classification_window -j2
.../test_classification_window.cpp:178:30: error:
  ‘class QgsClassificationMainWindow’ has no member named ‘startPostProcessTask’
.../test_classification_window.cpp:209:30: error:
  ‘class QgsClassificationMainWindow’ has no member named ‘startCrossValidationTask’
```

## GREEN evidence

After implementing the two APIs and terminal listener branches:

```text
$ cmake --build build-task-center-26 --target test_classification_window -j2
[100%] Built target test_classification_window

$ QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_classification_window "[task_center]"
(exit 0; this target's FastExitListener intentionally suppresses Catch output)
```

## Regression verification

```text
$ cmake --build build-task-center-26 --target test_task_center \
    test_classification_task_center test_classification_window \
    test_post_process test_cross_validation -j2
(all five targets built successfully)

$ build-task-center-26/tests/test_task_center
All tests passed (35 assertions in 8 test cases)

$ build-task-center-26/tests/test_classification_task_center
All tests passed (24 assertions in 6 test cases)

$ QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_classification_window
(exit 0; FastExitListener)

$ build-task-center-26/tests/test_post_process
All tests passed (7 assertions in 3 test cases)

$ build-task-center-26/tests/test_cross_validation
All tests passed (8 assertions in 4 test cases)

$ cmake --build build-task-center-26 --target sicnu_geo_rs -j2
[100%] Built target sicnu_geo_rs
```

Scoped source checks confirm the two public start methods call
`TaskCenter::instance().submitJob` and neither old UI path contains
`RsJobRunner::run`; there are no remaining `RsJobRunner::run` calls in the
Classification window. `git diff --check` exits successfully.

## Self-review and concerns

- The UI helper indirection is intentional: the requested public production
  seam owns callable submission, while the pre-existing private UI slots
  continue validating/preparing their dialog and ROI data before calling it.
- Cancellation remains worker-owned through `[task] { task->cancel(); }`.
  Task Center's #28 behavior keeps cancellation non-terminal until the worker
  actually returns, so the listener cannot delete a running worker.
- Existing build warnings (Qt deprecations and the QCA include-path warning)
  remain unrelated; all requested targets succeed.

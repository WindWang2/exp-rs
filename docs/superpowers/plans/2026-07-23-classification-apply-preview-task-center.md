# Classification Apply and Preview Task Center Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Classification full apply and viewport preview submit one Task Center-owned Algorithm Task each.

**Architecture:** Keep `RsClassificationTask` as the worker implementation, but submit its callable executor and cancellation hook through `TaskCenter`. The classification window keeps one pending-run record, consumes queued `TaskCenter::taskUpdated` terminal events, and applies the existing success, cancellation, and failure UI outcomes there. Post-processing and cross-validation remain on their current paths for #29.

**Tech Stack:** C++17, Qt 6 queued signals, QGIS tasks/layers, JsonCpp, Catch2.

## Global Constraints

- `applyClassification()` and `applyPreview()` create exactly one Task Center task and do not call `RsJobRunner`.
- Task Center remains the only caller-facing owner; JobEngine remains internal to it.
- Keep current output layer, workspace bridge, progress, cancellation, accuracy, and status-bar behavior.
- Do not migrate post-processing or cross-validation in this ticket.

### Task 1: Establish the Classification-to-Task-Center contract

**Files:**
- Modify: `tests/test_classification_window.cpp`

- [x] **Step 1: Write the failing migration contract test**

Assert that the full-apply and preview method bodies submit through `TaskCenter::submitJob` and contain no `RsJobRunner::run` call.

- [x] **Step 2: Run the focused test and verify it fails**

Run: `cmake --build build-task-center-26 --target test_classification_window -j2 && QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_classification_window "Task Center"`

Expected: failure because both methods still invoke `RsJobRunner::run`.

### Task 2: Migrate the two Classification execution paths

**Files:**
- Modify: `src/app/classification/qgsclassificationmainwindow.h`
- Modify: `src/app/classification/qgsclassificationmainwindow.cpp`
- Modify: `src/app/classification/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1: Store one pending Classification run and subscribe to Task Center terminal updates**

Keep the `RsClassificationTask` and its output metadata alive until the matching Task Center terminal event is handled. Connect queued updates in the window constructor.

- [x] **Step 2: Submit full apply through Task Center**

Use `TaskCenter::submitJob(request, executor, cancelHook)`. Preserve the current output result payload and full-apply workspace, layer, accuracy, structured-log, and status UI behavior in the terminal handler.

- [x] **Step 3: Submit preview through Task Center**

Use the same seam and preserve temporary preview layer replacement plus terminal status messages.

- [x] **Step 4: Complete terminal behavior from `AlgorithmTaskInfo`**

Use `Completed`, `Canceled`, and `Failed` state plus `errorMessage`, clean up the worker exactly once, and clear busy state before applying visible outcomes.

- [x] **Step 5: Link Classification with the Task Center library**

Add `sicnu_task_center` wherever the classification static library/test requires the symbols.

### Task 3: Verify and commit

- [x] **Step 1: Run the focused Classification and Task Center tests**

Run: `cmake --build build-task-center-26 --target test_classification_window test_task_center -j2 && QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_classification_window && QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_task_center`

- [x] **Step 2: Confirm only #28 methods lost the direct runner seam**

Run a scoped source inspection of `applyClassification()` and `applyPreview()`; post-process and cross-validation are expected to retain `RsJobRunner` until #29.

- [x] **Step 3: Compile the application**

Run: `cmake --build build-task-center-26 --target sicnu_geo_rs -j2`

- [ ] **Step 4: Commit the ticket**

```bash
git add docs/superpowers/plans/2026-07-23-classification-apply-preview-task-center.md src/app/classification/qgsclassificationmainwindow.* src/app/classification/CMakeLists.txt tests/test_classification_window.cpp tests/CMakeLists.txt
git commit -m "feat: route classification apply and preview through Task Center (#28)"
```

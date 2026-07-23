# Generic Dialog Task Center Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route `RasterProcessingDialogBase` GDAL, Processing, and RS-operator operations through one Task Center-owned Algorithm Task.

**Architecture:** Extend Task Center with a callable-job overload so UI code can keep its existing worker lambdas without reaching `JobEngine`. The dialog stores a Task Center task id and consumes queued terminal updates; it obtains output and errors from `AlgorithmTaskInfo`, preserving its existing `onCompleted` / `onFailed` UI outcome path.

**Tech Stack:** C++20, Qt 6 queued signals, QGIS processing APIs, JsonCpp, Catch2.

## Global Constraints

- `RasterProcessingDialogBase` must not call `RsJobRunner` or `JobEngine` after migration.
- Each dialog submission must create exactly one `AlgorithmTaskInfo` owned by `TaskCenter`.
- Worker execution remains inside `JobEngine`, which is an implementation detail of Task Center.
- Completion, cancellation, errors, progress, and logs are observed from Task Center.

---

### Task 1: Add callable execution to Task Center

**Files:**
- Modify: `src/processing/framework/task_center.h`
- Modify: `src/processing/framework/task_center.cpp`
- Modify: `tests/test_task_center.cpp`

**Consumes:** `sicnu::jobs::JobRequest`, `JobEngine::JobExecutor`, `JobEngine::CancelHook`.

**Produces:** `TaskCenter::submitJob(JobRequest, JobExecutor, CancelHook)` returning one Task Center task id and preserving the submitted JobEngine result, progress, logs, and terminal state.

- [x] **Step 1: Write the failing callable-submission test**

```cpp
const auto before = TaskCenter::instance().allTasks().size();
const long taskId = TaskCenter::instance().submitJob(request, executor);
REQUIRE(taskId > 0);
REQUIRE(TaskCenter::instance().allTasks().size() == before + 1);
REQUIRE(TaskCenter::instance().getTaskInfo(taskId).status == TaskStatus::Completed);
```

- [x] **Step 2: Run the focused test and verify it fails because the overload is absent**

Run: `cmake --build build-task-center-26 --target test_task_center -j2 && QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_task_center "callable"`

Expected: compilation failure stating `TaskCenter::submitJob` accepts only one argument.

- [x] **Step 3: Implement the overload by sharing the existing JobEngine polling and TaskCenter state updates**

```cpp
long submitJob(const jobs::JobRequest& request,
               jobs::JobEngine::JobExecutor executor,
               jobs::JobEngine::CancelHook onCancel = {});
```

Submit through `JobEngine::submit(request, executor, onCancel)`, retain the `jobId` and request in `AlgorithmTaskInfo`, then forward snapshot progress, log lines, result payload, errors, and cancellation through the same Task Center methods used by the non-callable overload.

- [x] **Step 4: Run the focused Task Center tests and verify green**

Run: `cmake --build build-task-center-26 --target test_task_center -j2 && QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_task_center`

Expected: all Task Center tests pass.

### Task 2: Migrate RasterProcessingDialogBase to Task Center

**Files:**
- Modify: `src/app/dialogs/raster_processing_dialog_base.h`
- Modify: `src/app/dialogs/raster_processing_dialog_base.cpp`
- Modify: `tests/test_raster_processing_dialog_base.cpp`
- Modify: `tests/CMakeLists.txt`

**Consumes:** callable and operator `TaskCenter::submitJob` overloads from Task 1; `AlgorithmTaskInfo::resultPayload` and `errorMessage`.

**Produces:** `RasterProcessingDialogBase` with `m_pendingTaskId`, one queued Task Center update connection, and terminal handling that preserves existing success and failure hooks.

- [x] **Step 1: Write the failing dialog seam test**

```cpp
const auto before = TaskCenter::instance().allTasks().size();
dialog.runGdalTask([] { return QStringLiteral("/tmp/test_out.tif"); });
REQUIRE(dialog.pendingTaskId() > 0);
REQUIRE(TaskCenter::instance().allTasks().size() == before + 1);
```

- [x] **Step 2: Run the focused dialog test and verify it fails because no Task Center task id exists**

Run: `cmake --build build-task-center-26 --target test_raster_processing_dialog_base -j2 && QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_raster_processing_dialog_base "runGdalTask"`

Expected: compilation failure for the missing test accessor or assertion failure because the old path only records a JobEngine id.

- [x] **Step 3: Replace RsJobRunner submission and JobEngine ids with Task Center submission and task terminal handling**

```cpp
connect(&TaskCenter::instance(), &TaskCenter::taskUpdated,
        this, &RasterProcessingDialogBase::onTaskUpdated, Qt::QueuedConnection);
m_pendingTaskId = TaskCenter::instance().submitJob(req, body);
```

On `Completed`, call `onCompleted` with `resultPayload["output"]`; on `Canceled` or `Failed`, call `onFailed` with `errorMessage` or the existing fallback text. Leave `handleCompleted` and `handleFailed` as the sole UI outcome handlers.

- [x] **Step 4: Link the dialog test with `sicnu_task_center` and remove its obsolete bridge source**

```cmake
target_link_libraries(test_raster_processing_dialog_base PRIVATE sicnu_task_center)
```

Remove `src/app/shell/job_engine_qt_bridge.cpp` from that test target because the dialog no longer uses it.

- [x] **Step 5: Run dialog and Task Center tests and verify green**

Run: `cmake --build build-task-center-26 --target test_raster_processing_dialog_base test_task_center -j2 && QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_raster_processing_dialog_base && QT_QPA_PLATFORM=offscreen build-task-center-26/tests/test_task_center`

Expected: all dialog and Task Center tests pass.

### Task 3: Compile the application and close out the ticket

**Files:** No production source changes expected.

- [x] **Step 1: Confirm the migrated dialog has no direct JobEngine or RsJobRunner submission**

Run: `rg -n 'RsJobRunner|JobEngine::instance\(\)\.submit|m_pendingJobId' src/app/dialogs/raster_processing_dialog_base.*`

Expected: no matches.

- [x] **Step 2: Compile the application target**

Run: `cmake --build build-task-center-26 --target sicnu_geo_rs -j2`

Expected: `Built target sicnu_geo_rs`.

- [x] **Step 3: Commit the ticket**

```bash
git add src/processing/framework/task_center.* src/app/dialogs/raster_processing_dialog_base.* tests/test_task_center.cpp tests/test_raster_processing_dialog_base.cpp tests/CMakeLists.txt
git commit -m "feat: route generic dialogs through Task Center (#27)"
```

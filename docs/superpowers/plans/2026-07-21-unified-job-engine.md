# Unified Job Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a process-local JobEngine that runs modular algorithms (`RSOperator` + adapters) on a limited worker pool, exposes a unified task dock with per-job logs, and migrates all algorithm entry points off ad-hoc threads.

**Architecture:** New `sicnu_jobs` library owns `JobEngine` (queue, workers, cancel, progress, log lines). UI uses `JobEngineQtBridge` + `RsJobPanel`. Algorithm bodies stay in `RSOperator`; Processing toolbox uses an adapter. Workflow/TaskPanel/dialogs call `submit` instead of `std::thread` / `AsyncGdalRunner`.

**Tech Stack:** C++20 / JsonCPP / Catch2 / Qt6 (UI bridge only) / existing `sicnu_operators` / optional QgsProcessing

**Spec:** `docs/superpowers/specs/2026-07-21-unified-job-engine-design.md`

---

## Global Constraints

- JobEngine: **no Qt Widgets**; prefer no Qt in `src/jobs/` (callbacks + mutex). Qt bridge in `src/app/shell/`.
- Default `maxWorkers = 3` (clamp 2–4). Support `exclusive` jobs.
- Display stretch is **not** a job.
- After Task 5 (TaskPanel wiring), **no new** algorithm threads outside JobEngine.
- Build: `cd build && cmake .. && make -j$(nproc) <target>`
- Test: `QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R job`
- Commits: `feat(jobs):` / `feat(shell):` / `test(jobs):` / `refactor(jobs):`
- TDD for engine core

---

## File map

| Path | Wave | Action | Responsibility |
|------|------|--------|----------------|
| `src/jobs/CMakeLists.txt` | W1 | Create | `sicnu_jobs` library |
| `src/jobs/job_types.h` | W1 | Create | JobRequest, JobState, JobRecord, log line |
| `src/jobs/job_engine.h/.cpp` | W1 | Create | submit/cancel/list/workers |
| `CMakeLists.txt` | W1 | Modify | `add_subdirectory(src/jobs)` |
| `tests/test_job_engine.cpp` | W1 | Create | Catch2 scheduling tests |
| `tests/CMakeLists.txt` | W1 | Modify | Register test |
| `src/app/shell/job_engine_qt_bridge.h/.cpp` | W1 | Create | QObject signals on main thread |
| `src/app/shell/rs_job_panel.h/.cpp` | W1 | Create | Task dock UI |
| `src/app/main_window_docks.cpp` | W1 | Modify | Install dock |
| `src/app/CMakeLists.txt` | W1 | Modify | Sources + link `sicnu_jobs` |
| `src/app/shell/workflow_session_controller.cpp` | W2 | Modify | submit instead of std::thread |
| `src/workflow/workflow_runtime.*` | W2 | Modify | optional async submit helper |
| `src/app/dialogs/raster_processing_dialog_base.*` | W3 | Modify | runOperatorTask → JobEngine |
| `src/app/classification/*` | W4 | Modify | wrap apply/train as jobs |
| `src/app/georeferencer/*` | W4 | Modify | warp job |
| `src/app/obia/*` | W4 | Modify | segment/classify jobs |
| `src/jobs/processing_job_adapter.*` | W5 | Create | QgsProcessing → job |
| `src/app/main_window_docks.cpp` (toolbox) | W5 | Modify | double-click submit |
| `src/app/dialogs/async_*.cpp` | W6 | Remove/retire | after migration |

---

## Wave W1 — JobEngine + Task Dock

### Task 1: Scaffold `sicnu_jobs` + types

**Files:**
- Create: `src/jobs/job_types.h`
- Create: `src/jobs/CMakeLists.txt`
- Create: `src/jobs/job_engine.cpp` (stub)
- Create: `src/jobs/job_engine.h` (forward declare)
- Modify: root `CMakeLists.txt` after `src/operators`
- Create: `tests/test_job_engine.cpp` smoke
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1.1: Write `job_types.h`**

```cpp
// src/jobs/job_types.h
#pragma once
#include <json/json.h>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace sicnu::jobs {

enum class JobState { Queued, Running, Succeeded, Failed, Cancelled };

enum class JobLogLevel { Info, Warning, Error };

struct JobLogLine {
  int64_t unixMs = 0;
  JobLogLevel level = JobLogLevel::Info;
  std::string text;
};

struct JobRequest {
  std::string algorithmId;
  Json::Value params{Json::objectValue};
  std::string title;
  std::string source; // ui|task_panel|dialog|toolbox|module|mcp|workflow
  bool exclusive = false;
  std::string clientTag;
};

struct JobRecord {
  std::string id;
  JobRequest request;
  JobState state = JobState::Queued;
  double progress = -1.0; // -1 = indeterminate
  std::string statusMessage;
  std::vector<JobLogLine> logLines;
  Json::Value result;
  std::string error;
  int64_t createdAtMs = 0;
  int64_t startedAtMs = 0;
  int64_t finishedAtMs = 0;
};

inline int64_t nowUnixMs()
{
  using namespace std::chrono;
  return duration_cast<milliseconds>( system_clock::now().time_since_epoch() ).count();
}

} // namespace sicnu::jobs
```

- [ ] **Step 1.2: CMake**

```cmake
# src/jobs/CMakeLists.txt
add_library(sicnu_jobs STATIC job_engine.cpp)
target_include_directories(sicnu_jobs PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
if(TARGET jsoncpp)
  target_link_libraries(sicnu_jobs PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(sicnu_jobs PUBLIC ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(sicnu_jobs PUBLIC ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(sicnu_jobs PUBLIC jsoncpp)
endif()
if(TARGET sicnu_operators)
  target_link_libraries(sicnu_jobs PUBLIC sicnu_operators)
endif()
set_target_properties(sicnu_jobs PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
```

Root: `add_subdirectory(src/jobs)` next to operators.

- [ ] **Step 1.3: Stub engine**

```cpp
// job_engine.h — minimal for smoke; expand in Task 2
#pragma once
#include "job_types.h"
namespace sicnu::jobs {
class JobEngine {
public:
  static JobEngine &instance();
  // Task 2 fills API
};
}
```

```cpp
// job_engine.cpp
#include "job_engine.h"
namespace sicnu::jobs {
JobEngine &JobEngine::instance() {
  static JobEngine e;
  return e;
}
}
```

- [ ] **Step 1.4: Smoke test + CMake** (mirror `test_workflow_runtime`)

```cpp
#include <catch2/catch_test_macros.hpp>
#include "jobs/job_types.h"
TEST_CASE("job types compile", "[job]") {
  sicnu::jobs::JobRequest r;
  r.algorithmId = "test:noop";
  REQUIRE(r.algorithmId == "test:noop");
}
```

- [ ] **Step 1.5: Build & run**

```bash
cd build && cmake .. && make -j$(nproc) test_job_engine && ./tests/test_job_engine
```

Expected: PASS

- [ ] **Step 1.6: Commit**

```bash
git add src/jobs CMakeLists.txt tests/test_job_engine.cpp tests/CMakeLists.txt
git commit -m "feat(jobs): scaffold sicnu_jobs and job types"
```

---

### Task 2: JobEngine scheduling (TDD)

**Files:**
- Fill: `src/jobs/job_engine.h/.cpp`
- Modify: `tests/test_job_engine.cpp`

Public API:

```cpp
// job_engine.h
#pragma once
#include "job_types.h"
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <unordered_map>
#include <atomic>
#include <optional>

namespace sicnu::jobs {

class JobEngine {
public:
  using Listener = std::function<void(const JobRecord &)>;

  static JobEngine &instance();

  void setMaxWorkers(int n); // clamp 2..4
  int maxWorkers() const;

  std::string submit(JobRequest req);
  bool cancel(const std::string &jobId);
  std::optional<JobRecord> snapshot(const std::string &jobId) const;
  std::vector<JobRecord> list() const;

  // Listener called from worker threads — bridge must marshal to GUI thread
  void setListener(Listener listener);

  // Test helpers
  void waitUntilIdleForTests(int timeoutMs = 10000);
  void shutdownForTests(); // join workers, only in tests

private:
  JobEngine();
  ~JobEngine();
  JobEngine(const JobEngine &) = delete;
  JobEngine &operator=(const JobEngine &) = delete;

  void workerLoop();
  void scheduleLocked(); // requires m_mutex
  void appendLog(JobRecord &rec, JobLogLevel level, const std::string &text);
  void notify(const JobRecord &rec);
  void runOperatorJob(JobRecord &rec);

  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::deque<std::string> m_queue;
  std::unordered_map<std::string, JobRecord> m_jobs;
  std::vector<std::thread> m_workers;
  Listener m_listener;
  int m_maxWorkers = 3;
  int m_running = 0;
  bool m_exclusiveRunning = false;
  std::atomic<bool> m_stop{false};
  std::atomic<uint64_t> m_nextId{1};
};

} // namespace sicnu::jobs
```

- [ ] **Step 2.1: Failing tests** (register a tiny test operator in test file like `test_rs_operator`)

```cpp
// In test file: TestSleepOperator name "test:sleep" params ms, sleeps, checks cancel
// TestAddOperator "test:add" as in test_rs_operator

TEST_CASE("submit runs operator and succeeds", "[job]") {
  // register test:add
  auto &eng = JobEngine::instance();
  eng.setMaxWorkers(2);
  JobRequest req;
  req.algorithmId = "test:add";
  req.params["a"] = 2;
  req.params["b"] = 3;
  req.title = "add";
  req.source = "test";
  auto id = eng.submit(req);
  REQUIRE_FALSE(id.empty());
  eng.waitUntilIdleForTests();
  auto snap = eng.snapshot(id);
  REQUIRE(snap.has_value());
  REQUIRE(snap->state == JobState::Succeeded);
  REQUIRE(snap->result.isMember("result"));
}

TEST_CASE("max workers limits concurrency", "[job]") {
  // submit 4 slow test:sleep(200) with maxWorkers=2
  // track peak concurrent via atomic in operator or log "start"/"end"
  // REQUIRE peak <= 2
}

TEST_CASE("cancel cooperative", "[job]") {
  // long sleep job, cancel while running, expect Cancelled or Failed with cancel
}

TEST_CASE("failed operator sets error", "[job]") {
  // missing params → Failed + non-empty error
}
```

- [ ] **Step 2.2: Implement engine**

`runOperatorJob`:

```cpp
auto op = sicnu::operators::RSOperatorRegistry::instance().create(rec.request.algorithmId);
if (!op) { rec.state = Failed; rec.error = "Unknown algorithm"; return; }
std::atomic<bool> cancel{false};
sicnu::operators::RSOperatorContext ctx;
ctx.setCancelFlag(&cancel);
// store cancel flag pointer in side map keyed by jobId for cancel()
ctx.setLogCallback([&](level, msg){ appendLog under lock; notify; });
ctx.setProgressCallback([&](p, msg){ update progress under lock; notify; });
try {
  rec.result = op->run(rec.request.params, ctx);
  if (cancel.load()) rec.state = Cancelled;
  else rec.state = Succeeded;
} catch (const sicnu::operators::RSOperatorError &e) {
  rec.state = Failed;
  rec.error = e.what(); // use message() if available
} catch (const std::exception &e) {
  rec.state = Failed;
  rec.error = e.what();
}
```

`cancel`: set atomic for that job; if still queued, mark Cancelled and remove from queue.

- [ ] **Step 2.3: Tests PASS**

```bash
make -j$(nproc) test_job_engine && ./tests/test_job_engine
```

- [ ] **Step 2.4: Commit**

```bash
git commit -am "feat(jobs): JobEngine submit/cancel/workers and operator execution"
```

---

### Task 3: Qt bridge + RsJobPanel

**Files:**
- Create: `src/app/shell/job_engine_qt_bridge.h/.cpp`
- Create: `src/app/shell/rs_job_panel.h/.cpp`
- Modify: `main_window_docks.cpp`, `main_window.h`, `CMakeLists.txt` app
- Modify: `applyProductShellLayout` — **show** job dock by default (or bottom raised)

**JobEngineQtBridge**

```cpp
class JobEngineQtBridge : public QObject {
  Q_OBJECT
public:
  static JobEngineQtBridge *instance(); // creates, installs listener once
signals:
  void jobUpdated(const QString &jobId); // payload via snapshot fetch
  void jobFinished(const QString &jobId);
private:
  JobEngineQtBridge();
};
```

Listener: `QMetaObject::invokeMethod(bridge, ..., Qt::QueuedConnection)` with jobId string.

**RsJobPanel** : `QgsDockWidget` or `QDockWidget`

```
QHBox split:
  QTreeWidget/QListWidget jobs
  QPlainTextEdit log
Bottom: Cancel | Clear finished | filter combo
```

On `jobUpdated`: refresh row + if selected append log.
On select: fill log from snapshot.logLines.

- [ ] **Step 3.1: Implement bridge + panel**
- [ ] **Step 3.2: setupDockWidgets or setupRibbonAndTaskPanel**

```cpp
m_jobPanel = new RsJobPanel(this);
addDockWidget(Qt::BottomDockWidgetArea, m_jobPanel);
// tabify with log if both exist, raise job panel
JobEngineQtBridge::instance(); // ensure listener
```

- [ ] **Step 3.3: Build app**

```bash
make -j$(nproc) sicnu_geo_rs
```

- [ ] **Step 3.4: Commit**

```bash
git commit -am "feat(shell): job panel dock and JobEngine Qt bridge"
```

---

## Wave W2 — TaskPanel / operators through engine

### Task 4: TaskPanel Run → submit

**Files:**
- Modify: `src/app/shell/workflow_session_controller.cpp`

Replace `std::thread` + `m_runtime.runStep` with:

```cpp
sicnu::jobs::JobRequest req;
req.algorithmId = opId.toStdString(); // from current step
req.params = panel->formValues(); // convert Json
req.title = windowTitle;
req.source = "task_panel";
const auto jobId = sicnu::jobs::JobEngine::instance().submit(req);
// connect JobEngineQtBridge::jobFinished for this id → load layer / panel success
// panel setRunning(true) until finished
```

Keep gate validation via `m_runtime.canRun` before submit.
On success: still `setArtifact` / markStepComplete on workflow session.

- [ ] **Step 4.1: Implement**
- [ ] **Step 4.2: Manual or smoke: open spectral index, run, see job panel**
- [ ] **Step 4.3: Commit**

```bash
git commit -am "feat(shell): TaskPanel executes algorithms via JobEngine"
```

### Task 5: Dialog `runOperatorTask` → JobEngine

**Files:**
- Modify: `src/app/dialogs/raster_processing_dialog_base.cpp` `runOperatorTask`

```cpp
void RasterProcessingDialogBase::runOperatorTask(const QString &operatorId, const Json::Value &params)
{
  startRun();
  sicnu::jobs::JobRequest req;
  req.algorithmId = operatorId.toStdString();
  req.params = params;
  req.title = dialogTitle().toStdString();
  req.source = "dialog";
  const std::string jobId = sicnu::jobs::JobEngine::instance().submit(req);
  // store jobId member; connect once to bridge finished → onCompleted/onFailed
}
```

- [ ] **Step 5.1: Implement + keep API for subclasses**
- [ ] **Step 5.2: Build; spot-check one dialog (if still used) or mosaic path**
- [ ] **Step 5.3: Commit**

```bash
git commit -am "refactor(dialogs): runOperatorTask submits to JobEngine"
```

---

## Wave W3 — Remaining dialogs / ensure operators

### Task 6: Inventory + migrate leftover dialogs

**Files:** all under `src/app/dialogs/*` still using `AsyncGdalRunner` without operator

- [ ] **Step 6.1:** `rg "AsyncGdalRunner|runGdalTask|std::thread" src/app/dialogs`
- [ ] **Step 6.2:** For each hit: either call `runOperatorTask` with existing operator id, or add thin RSOperator wrapping existing code, then submit
- [ ] **Step 6.3:** Commit per group

```bash
git commit -am "refactor(dialogs): migrate remaining tools to JobEngine"
```

---

## Wave W4 — Modules

### Task 7: Classification apply → job

**Files:**
- Create operator or job wrapper `module:classify:apply` OR submit with algorithm id wrapping `RsClassificationTask` logic
- Modify: `qgsclassificationmainwindow.cpp` apply path

Minimal approach:

```cpp
// On Apply: build params JSON from session; submit algorithm "rs:supervised_classification"
// if already an operator; else register ModuleClassifyApplyOperator that runs existing task body
```

- [ ] **Step 7.1:** Prefer existing `rs:supervised_classification` if usable
- [ ] **Step 7.2:** Wire UI to JobEngine; progress in job panel
- [ ] **Step 7.3:** Commit

```bash
git commit -am "feat(classify): run training/apply through JobEngine"
```

### Task 8: Georef warp + OBIA

Same pattern: warp success path and OBIA segment/classify submit jobs with exclusive=true for warp.

- [ ] **Step 8.1: Georef**
- [ ] **Step 8.2: OBIA**
- [ ] **Step 8.3: Commit**

```bash
git commit -am "feat(modules): georef and OBIA jobs on JobEngine"
```

---

## Wave W5 — Processing toolbox

### Task 9: Processing adapter

**Files:**
- Create: `src/jobs/processing_job_adapter.h/.cpp`
- Link processing headers carefully (may need app or gui lib — if heavy, put adapter in app as `ProcessingJobSubmitter`)

If linking QgsProcessing from `sicnu_jobs` is too heavy, implement adapter in `src/app/shell/processing_job_submitter.cpp` that only uses JobEngine for queue + runs processing on worker with QGIS rules.

```cpp
// Prefer app-side runner registered as fake operator id "processing:run"
// OR JobEngine supports pluggable Executor interface:
```

**Pluggable executor (recommended if QGIS deps heavy):**

```cpp
// job_engine.h
using Executor = std::function<Json::Value(const JobRequest&, ProgressFn, LogFn, CancelFlag&)>;
void registerExecutor(const std::string &prefix, Executor ex);
// default executor: RSOperator for ids without prefix match
// "processing:" → app registers executor at startup
```

- [ ] **Step 9.1: Add registerExecutor to JobEngine + test with mock prefix**
- [ ] **Step 9.2: App registers processing executor**
- [ ] **Step 9.3: Toolbox doubleClick → parameter dialog → submit**
- [ ] **Step 9.4: Commit**

```bash
git commit -am "feat(jobs): processing adapter and toolbox submit path"
```

---

## Wave W6 — Cleanup

### Task 10: Remove dual paths

- [ ] **Step 10.1:** `rg AsyncGdalRunner|AsyncAlgorithmRunner` — remove dead code or leave thin deprecated stub that asserts false
- [ ] **Step 10.2:** Document in `docs/dialog-base-class.md` that Run uses JobEngine
- [ ] **Step 10.3:** Final test suite

```bash
ctest --output-on-failure -R 'job|workflow|spectral|band_math'
```

- [ ] **Step 10.4: Commit**

```bash
git commit -am "refactor(jobs): retire legacy async runners and document JobEngine"
```

---

## Spec coverage

| Spec | Tasks |
|------|-------|
| JobEngine core | 1–2 |
| Limited parallel + exclusive | 2 |
| RsJobPanel + logs | 3 |
| RSOperator via engine | 2, 4–5 |
| Dialogs | 5–6 |
| Modules | 7–8 |
| Processing | 9 |
| Retire dual path | 10 |
| Display stretch not job | — (no change) |

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-21-unified-job-engine.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — this session with executing-plans and checkpoints  

Which approach?

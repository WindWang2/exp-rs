# UI Workflow Engine + Product Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a Qt-free Workflow Runtime, a six-tab Ribbon + right-side TaskPanel host, schema-driven forms for atomic RS tools, and staged migration of classify/georef modules onto the same session model.

**Architecture:** New `src/workflow/` library owns definition/session/gate/runner (JSON + `RSOperatorRegistry`). App shell adds `RibbonController` + `TaskPanelHost` + `SchemaFormBuilder` that project session state into widgets. Classification and georeferencer keep their windows but bind Stepper/gates to Runtime definitions. No Model Builder, no DAG scheduler.

**Tech Stack:** C++20 / Qt6 Widgets / JsonCPP / Catch2 / existing `sicnu_operators` / QGIS canvas

**Spec:** `docs/superpowers/specs/2026-07-21-ui-workflow-engine-design.md`

---

## Global Constraints

- Runtime (`src/workflow/`) must **not** include Qt Widgets headers; prefer no Qt at all (JsonCPP + std). If logging needs Qt later, only `QString` conversion at the UI boundary.
- Build: `cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc) <target>`
- Test: `cd /home/kevin/projects/exp-rs/build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R '<pattern>'`
- Commits: `feat(workflow):` / `feat(shell):` / `test(workflow):` / `docs(workflow):`
- TDD for Runtime: red → green → commit
- Do not rewrite classify/georef algorithms
- Theme: extend `resources/styles.qss` (existing RS Studio tokens); add objectName rules for ribbon/panel — do not reintroduce obsolete lavender palette from old slate doc
- New RS tools must register operator + workflow definition; no dialog-only tools

---

## File map

| Path | Phase | Action | Responsibility |
|------|-------|--------|----------------|
| `src/workflow/CMakeLists.txt` | W0 | Create | Library target `sicnu_workflow` |
| `src/workflow/workflow_types.h` | W0 | Create | Enums, ids, snapshot structs |
| `src/workflow/workflow_definition.h/.cpp` | W0 | Create | Immutable definition + steps |
| `src/workflow/workflow_gate.h/.cpp` | W0 | Create | Gate predicates |
| `src/workflow/workflow_session.h/.cpp` | W0 | Create | Mutable session state |
| `src/workflow/workflow_registry.h/.cpp` | W0 | Create | Definition registry |
| `src/workflow/workflow_runtime.h/.cpp` | W0 | Create | open/goto/setParams/canRun/run |
| `src/workflow/workflow_runner.h/.cpp` | W0 | Create | Sync operator execution (async later in UI) |
| `src/workflow/builtin_definitions.cpp` | W0–W1 | Create | Register catalog + first tools |
| `CMakeLists.txt` (root) | W0 | Modify | `add_subdirectory(src/workflow)` |
| `tests/test_workflow_runtime.cpp` | W0 | Create | Catch2 pure logic tests |
| `tests/CMakeLists.txt` | W0 | Modify | Link `sicnu_workflow` |
| `src/app/shell/ribbon_controller.h/.cpp` | W1 | Create | Six tabs + actions |
| `src/app/shell/task_panel_host.h/.cpp` | W1 | Create | Right dock host |
| `src/app/shell/schema_form_builder.h/.cpp` | W1 | Create | schema JSON → form widgets |
| `src/app/shell/workflow_session_controller.h/.cpp` | W1 | Create | Bridge Runtime ↔ UI + async run |
| `src/app/main_window.h/.cpp` | W1 | Modify | Install ribbon + task panel |
| `src/app/main_window_menus.cpp` | W1 | Modify | Wire ribbon groups; keep menus as secondary |
| `src/app/main_window_docks.cpp` | W1 | Modify | Right dock for TaskPanel |
| `src/app/CMakeLists.txt` | W1 | Modify | Sources + link `sicnu_workflow` |
| `resources/styles.qss` | W1 | Modify | `#rsRibbon*`, `#rsTaskPanel*` rules |
| `src/app/classification/*` | W3 | Modify | Bind to Runtime definition |
| `src/app/georeferencer/*` | W4 | Modify | Bind to Runtime definition |
| `data/workflows/` | W2+ | Optional | JSON definitions if code catalog grows |

---

## Phase W0 — Workflow Runtime (no GUI)

### Task 1: Scaffold `sicnu_workflow` library + empty test

**Files:**
- Create: `src/workflow/CMakeLists.txt`
- Create: `src/workflow/workflow_types.h`
- Create: `tests/test_workflow_runtime.cpp` (minimal)
- Modify: root `CMakeLists.txt` (add subdirectory near operators)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1.1: Create types header**

```cpp
// src/workflow/workflow_types.h
#pragma once
#include <json/json.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace sicnu::workflow {

enum class StepKind { Operator, Interactive, Review, Composite };
enum class HostKind { TaskPanel, Workspace };
enum class SessionMode { Wizard, Expert };

struct GateDef {
  std::string require; // e.g. "hasArtifact:output", "paramNonEmpty:input"
  std::string hint;
};

struct StepDef {
  std::string id;
  std::string title;
  StepKind kind = StepKind::Operator;
  std::string operatorId; // when kind == Operator
  std::vector<GateDef> gates;
  std::string artifactOnSuccess = "output"; // default artifact name from operator result["output"]
};

struct WorkflowDefinition {
  std::string id;
  std::string title;
  HostKind host = HostKind::TaskPanel;
  std::string workspaceKind; // classify | georef | obia | empty
  std::vector<StepDef> steps;
};

struct CanRunResult {
  bool ok = true;
  std::vector<std::string> hints;
};

struct SessionSnapshot {
  std::string sessionId;
  std::string definitionId;
  std::string currentStepId;
  std::vector<std::string> completedStepIds;
  SessionMode mode = SessionMode::Wizard;
  bool dirty = false;
  Json::Value paramsByStep; // object: stepId -> params object
  std::unordered_map<std::string, std::string> artifacts; // name -> path/value
};

} // namespace sicnu::workflow
```

- [ ] **Step 1.2: CMake for library**

```cmake
# src/workflow/CMakeLists.txt
set(SICNU_WORKFLOW_SOURCES
  workflow_definition.cpp
  workflow_gate.cpp
  workflow_session.cpp
  workflow_registry.cpp
  workflow_runner.cpp
  workflow_runtime.cpp
  builtin_definitions.cpp
)
add_library(sicnu_workflow STATIC ${SICNU_WORKFLOW_SOURCES})
target_include_directories(sicnu_workflow PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)
# Link jsoncpp the same way sicnu_operators does in this repo
if(TARGET jsoncpp)
  target_link_libraries(sicnu_workflow PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(sicnu_workflow PUBLIC ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(sicnu_workflow PUBLIC ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(sicnu_workflow PUBLIC jsoncpp)
endif()
# Optional: link sicnu_operators for runner (Task 5)
```

Add near other `add_subdirectory` calls in root CMake (search for `src/operators`):

```cmake
add_subdirectory(src/workflow)
```

- [ ] **Step 1.3: Stub cpp files** so the library links — empty `namespace sicnu::workflow {}` in each `.cpp` listed above until later tasks fill them.

- [ ] **Step 1.4: Minimal test + CMake entry** (mirror `test_rs_operator` linking pattern)

```cpp
// tests/test_workflow_runtime.cpp
#include <catch2/catch_test_macros.hpp>
#include "workflow/workflow_types.h"

TEST_CASE("workflow types compile", "[workflow]")
{
  sicnu::workflow::WorkflowDefinition d;
  d.id = "tool.test";
  REQUIRE(d.id == "tool.test");
}
```

```cmake
# tests/CMakeLists.txt — inside if(TARGET sicnu_operators) or always if workflow has no op dep yet
if(TARGET sicnu_workflow)
  add_executable(test_workflow_runtime test_workflow_runtime.cpp)
  target_link_libraries(test_workflow_runtime PRIVATE Catch2::Catch2WithMain sicnu_workflow)
  target_include_directories(test_workflow_runtime PRIVATE ${CMAKE_SOURCE_DIR}/src)
  sicnu_discover_tests(test_workflow_runtime)
endif()
```

- [ ] **Step 1.5: Build and run**

```bash
cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc) test_workflow_runtime
QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R test_workflow_runtime
```

Expected: PASS

- [ ] **Step 1.6: Commit**

```bash
git add src/workflow CMakeLists.txt tests/test_workflow_runtime.cpp tests/CMakeLists.txt
git commit -m "feat(workflow): scaffold sicnu_workflow library and smoke test"
```

---

### Task 2: Definition + Registry + Session goto/setParams

**Files:**
- Create/fill: `workflow_definition.h/.cpp`, `workflow_registry.h/.cpp`, `workflow_session.h/.cpp`
- Modify: `tests/test_workflow_runtime.cpp`

- [ ] **Step 2.1: Write failing tests**

```cpp
#include "workflow/workflow_definition.h"
#include "workflow/workflow_registry.h"
#include "workflow/workflow_session.h"

using namespace sicnu::workflow;

static WorkflowDefinition makeTwoStep()
{
  WorkflowDefinition d;
  d.id = "tool.demo";
  d.title = "Demo";
  d.host = HostKind::TaskPanel;
  StepDef a;
  a.id = "configure";
  a.title = "Configure";
  a.kind = StepKind::Operator;
  a.operatorId = "test:add";
  StepDef b;
  b.id = "review";
  b.title = "Review";
  b.kind = StepKind::Review;
  d.steps = {a, b};
  return d;
}

TEST_CASE("Registry stores definitions", "[workflow]")
{
  WorkflowRegistry reg;
  reg.registerDefinition(makeTwoStep());
  REQUIRE(reg.has("tool.demo"));
  REQUIRE_FALSE(reg.has("missing"));
  const auto *d = reg.find("tool.demo");
  REQUIRE(d != nullptr);
  REQUIRE(d->steps.size() == 2);
}

TEST_CASE("Session open starts at first step", "[workflow]")
{
  auto d = makeTwoStep();
  WorkflowSession s(d, "sess-1");
  auto snap = s.snapshot();
  REQUIRE(snap.sessionId == "sess-1");
  REQUIRE(snap.definitionId == "tool.demo");
  REQUIRE(snap.currentStepId == "configure");
  REQUIRE(snap.completedStepIds.empty());
}

TEST_CASE("Session goto and setParams mark dirty", "[workflow]")
{
  WorkflowSession s(makeTwoStep(), "sess-2");
  REQUIRE(s.gotoStep("review"));
  REQUIRE(s.snapshot().currentStepId == "review");
  REQUIRE_FALSE(s.gotoStep("nope"));
  Json::Value p;
  p["a"] = 1;
  p["b"] = 2;
  s.setParams("configure", p);
  REQUIRE(s.snapshot().dirty);
  REQUIRE(s.paramsFor("configure")["a"].asInt() == 1);
}
```

- [ ] **Step 2.2: Run tests — expect FAIL** (types missing)

```bash
make -j$(nproc) test_workflow_runtime && QT_QPA_PLATFORM=offscreen ./tests/test_workflow_runtime
```

- [ ] **Step 2.3: Implement definition helpers + registry + session**

```cpp
// workflow_registry.h (public API)
#pragma once
#include "workflow_definition.h"
#include <mutex>
#include <unordered_map>

namespace sicnu::workflow {
class WorkflowRegistry {
public:
  void registerDefinition(WorkflowDefinition def);
  bool has(const std::string& id) const;
  const WorkflowDefinition* find(const std::string& id) const;
  std::vector<std::string> ids() const;
private:
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, WorkflowDefinition> m_defs;
};
}
```

```cpp
// workflow_session.h (public API)
#pragma once
#include "workflow_types.h"
#include "workflow_definition.h"

namespace sicnu::workflow {
class WorkflowSession {
public:
  WorkflowSession(WorkflowDefinition def, std::string sessionId);
  SessionSnapshot snapshot() const;
  bool gotoStep(const std::string& stepId);
  void setParams(const std::string& stepId, const Json::Value& params);
  Json::Value paramsFor(const std::string& stepId) const;
  void setArtifact(const std::string& name, const std::string& value);
  bool hasArtifact(const std::string& name) const;
  void markStepComplete(const std::string& stepId);
  void setMode(SessionMode mode);
  void setDirty(bool d);
  const WorkflowDefinition& definition() const { return m_def; }
  const StepDef* currentStep() const;
  const StepDef* stepById(const std::string& id) const;
private:
  WorkflowDefinition m_def;
  std::string m_sessionId;
  std::string m_currentStepId;
  std::vector<std::string> m_completed;
  SessionMode m_mode = SessionMode::Wizard;
  bool m_dirty = false;
  Json::Value m_paramsByStep; // object
  std::unordered_map<std::string, std::string> m_artifacts;
};
}
```

Implement `.cpp` with straightforward logic: constructor sets `m_currentStepId = m_def.steps.front().id` if non-empty; `gotoStep` finds id in steps; `setParams` stores under step key and sets dirty; `markStepComplete` appends unique id.

- [ ] **Step 2.4: Run tests — expect PASS**

- [ ] **Step 2.5: Commit**

```bash
git add src/workflow tests/test_workflow_runtime.cpp
git commit -m "feat(workflow): definition registry and session navigation"
```

---

### Task 3: Gates (`canRun`) pure logic

**Files:**
- Create/fill: `workflow_gate.h/.cpp`
- Modify: `workflow_session` or free functions used by Runtime
- Modify: tests

- [ ] **Step 3.1: Failing tests**

```cpp
#include "workflow/workflow_gate.h"

TEST_CASE("Gate hasArtifact", "[workflow][gate]")
{
  WorkflowSession s(makeTwoStep(), "g1");
  GateDef g;
  g.require = "hasArtifact:output";
  g.hint = "需要先生成输出";
  auto r = evaluateGates(s, {g});
  REQUIRE_FALSE(r.ok);
  REQUIRE(r.hints.at(0).find("输出") != std::string::npos);
  s.setArtifact("output", "/tmp/out.tif");
  r = evaluateGates(s, {g});
  REQUIRE(r.ok);
}

TEST_CASE("Gate paramNonEmpty", "[workflow][gate]")
{
  WorkflowSession s(makeTwoStep(), "g2");
  GateDef g;
  g.require = "paramNonEmpty:configure.input";
  g.hint = "请选择输入";
  // configure params missing input
  REQUIRE_FALSE(evaluateGates(s, {g}).ok);
  Json::Value p;
  p["input"] = "/data/a.tif";
  s.setParams("configure", p);
  REQUIRE(evaluateGates(s, {g}).ok);
}
```

Support require forms:

| require | meaning |
|---------|---------|
| `hasArtifact:<name>` | artifact map contains non-empty value |
| `paramNonEmpty:<stepId>.<key>` | params string/path non-empty |
| `paramNonEmpty:<key>` | key on **current** step params |

- [ ] **Step 3.2: Implement `evaluateGates(const WorkflowSession&, const std::vector<GateDef>&)`**

- [ ] **Step 3.3: Tests PASS → commit**

```bash
git commit -am "feat(workflow): soft-gate evaluation helpers"
```

---

### Task 4: Runtime open/canRun + Runner with mock operator

**Files:**
- Fill: `workflow_runtime.h/.cpp`, `workflow_runner.h/.cpp`
- Modify: `src/workflow/CMakeLists.txt` to `target_link_libraries(sicnu_workflow PUBLIC sicnu_operators)` when target exists
- Modify: tests — register a tiny mock operator **in the test file** (same pattern as `test_rs_operator.cpp` `TestAddOperator`)

- [ ] **Step 4.1: Failing tests**

```cpp
#include "workflow/workflow_runtime.h"
#include "operators/framework/rs_operator_registry.h"
// ... TestAddOperator from test_rs_operator (copy class into anonymous namespace)

TEST_CASE("Runtime open and canRun respects step gates", "[workflow]")
{
  ensure operators registry has test:add
  WorkflowRegistry reg;
  WorkflowDefinition d = makeTwoStep();
  d.steps[0].gates.push_back({"paramNonEmpty:configure.a", "需要参数 a"});
  reg.registerDefinition(d);

  WorkflowRuntime rt(reg);
  auto id = rt.open("tool.demo");
  REQUIRE_FALSE(id.empty());
  auto can = rt.canRun(id, "configure");
  REQUIRE_FALSE(can.ok);

  Json::Value p; p["a"] = 1; p["b"] = 2;
  rt.setParams(id, "configure", p);
  can = rt.canRun(id, "configure");
  REQUIRE(can.ok);
}

TEST_CASE("Runtime run operator writes artifact", "[workflow]")
{
  // definition single operator step, gates empty, operator test:add
  // set params a=2 b=3, run, expect artifact or result stored
  // For add operator result is numeric — runner should store JSON string under artifact key "result"
  // Prefer: operator returns {"output": "..."} for file tools; for test:add store full result JSON as artifact "result"
}
```

- [ ] **Step 4.2: Public Runtime API**

```cpp
// workflow_runtime.h
class WorkflowRuntime {
public:
  explicit WorkflowRuntime(WorkflowRegistry& registry);
  std::string open(const std::string& definitionId);
  SessionSnapshot state(const std::string& sessionId) const;
  bool gotoStep(const std::string& sessionId, const std::string& stepId);
  void setParams(const std::string& sessionId, const std::string& stepId, const Json::Value& params);
  CanRunResult canRun(const std::string& sessionId, const std::string& stepId) const;
  // Synchronous run for unit tests; UI will call on worker thread
  Json::Value runStep(const std::string& sessionId, const std::string& stepId);
  void markStepComplete(const std::string& sessionId, const std::string& stepId);
  void close(const std::string& sessionId);
private:
  WorkflowRegistry& m_registry;
  std::unordered_map<std::string, std::unique_ptr<WorkflowSession>> m_sessions;
  int m_nextId = 1;
};
```

`runStep` logic:

1. Lookup session + step  
2. If `canRun` fails → throw `std::runtime_error` joining hints  
3. If kind != Operator → throw or no-op with clear error  
4. `WorkflowRunner::run(operatorId, params)` → create operator from `RSOperatorRegistry`, `RSOperatorContext`, catch `RSOperatorError`  
5. If result has `output` string → `setArtifact(artifactOnSuccess, path)`  
6. Else store `result.toStyledString()` under artifactOnSuccess or `"result"`  
7. `markStepComplete(stepId)`  
8. Return result JSON  

- [ ] **Step 4.3: Tests PASS → commit**

```bash
git commit -am "feat(workflow): runtime open/canRun/runStep via RSOperator"
```

---

### Task 5: Builtin catalog — first atomic definitions

**Files:**
- Fill: `builtin_definitions.cpp` + `builtin_definitions.h` with `void registerBuiltinWorkflows(WorkflowRegistry&)`
- Prefer operators that already exist: `rs:spectral_index`, `rs:band_math` (verify exact `name()` in operator sources)

- [ ] **Step 5.1: Grep operator ids**

```bash
rg 'return "rs:' src/operators -n
```

- [ ] **Step 5.2: Register single-step task_panel workflows** for at least:

1. Spectral index  
2. Band math  
3. One filter or change detection if id stable  

```cpp
WorkflowDefinition spectral;
spectral.id = "tool.rs.spectral_index";
spectral.title = "光谱指数";
spectral.host = HostKind::TaskPanel;
StepDef s;
s.id = "run";
s.title = "运行";
s.kind = StepKind::Operator;
s.operatorId = "rs:spectral_index"; // exact id from source
s.gates.push_back({"paramNonEmpty:run.input", "请选择输入栅格"});
// adjust param key to match operator schema
spectral.steps = {s};
reg.registerDefinition(spectral);
```

- [ ] **Step 5.3: Test `registerBuiltinWorkflows` makes ids findable**

- [ ] **Step 5.4: Commit**

```bash
git commit -am "feat(workflow): register builtin atomic tool definitions"
```

**W0 exit criteria:** `ctest -R test_workflow_runtime` all green; library linked; no app UI required.

---

## Phase W1 — Ribbon shell + TaskPanel + first tools

### Task 6: Design tokens / QSS for shell chrome

**Files:**
- Modify: `resources/styles.qss`

- [ ] **Step 6.1: Append rules** (use existing accent `#3a7f1a` from current QSS, not lavender)

```css
/* Ribbon */
#rsRibbonBar {
  background: #ffffff;
  border-bottom: 1px solid #e4e7eb;
}
#rsRibbonTabBar::tab:selected {
  color: #3a7f1a;
  font-weight: 600;
}
#rsRibbonGroupTitle {
  color: #8a92a0;
  font-size: 10px;
}

/* Task panel */
#rsTaskPanel {
  background: #ffffff;
  border-left: 1px solid #e4e7eb;
}
#rsTaskPanelTitle {
  font-size: 14px;
  font-weight: 600;
  color: #14171c;
}
#rsTaskPanelSectionLabel {
  color: #5b6473;
  font-size: 11px;
  margin-top: 8px;
}
QPushButton#rsTaskPanelRun[primary="true"] {
  background-color: #3a7f1a;
  color: #ffffff;
  border: none;
}
```

- [ ] **Step 6.2: Commit**

```bash
git commit -am "style(shell): QSS tokens for ribbon and task panel"
```

---

### Task 7: `SchemaFormBuilder` (schema → widgets)

**Files:**
- Create: `src/app/shell/schema_form_builder.h/.cpp`
- Optional test later; manual verify in Task 9

- [ ] **Step 7.1: API**

```cpp
// schema_form_builder.h
#pragma once
#include <QWidget>
#include <QVariantMap>
#include <json/json.h>

class QFormLayout;
class QgsMapLayer;

class SchemaFormBuilder : public QWidget
{
  Q_OBJECT
public:
  explicit SchemaFormBuilder(QWidget *parent = nullptr);
  /** Build controls from RSOperator::schema() root object. */
  void rebuild(const Json::Value &schema);
  Json::Value values() const;
  void setValues(const Json::Value &params);
  void setRasterLayerChoices(const QStringList &layerIds, const QStringList &layerNames);
signals:
  void valuesChanged();
private:
  QVBoxLayout *m_root = nullptr;
  // track editors by param name for values()
};
```

Mapping heuristics (when `x-ui-widget` absent):

- schema type string + format path/file → path line edit + browse  
- makeRasterParam style descriptions / title contains "raster" / name `input` → layer combo  
- enum → QComboBox  
- number/integer → spin  
- boolean → checkbox  

Group order: collect fields with `x-ui-group` or name heuristics into sections **输入 / 参数 / 输出** as `QGroupBox`.

- [ ] **Step 7.2: Implement rebuild/values/setValues**

- [ ] **Step 7.3: Wire into app CMake**

- [ ] **Step 7.4: Commit**

```bash
git commit -am "feat(shell): schema-driven form builder for task panel"
```

---

### Task 8: `TaskPanelHost` + `WorkflowSessionController`

**Files:**
- Create: `src/app/shell/task_panel_host.h/.cpp`
- Create: `src/app/shell/workflow_session_controller.h/.cpp`

- [ ] **Step 8.1: TaskPanelHost UI skeleton**

```
QWidget#rsTaskPanel
  QLabel#rsTaskPanelTitle
  QLabel help summary
  SchemaFormBuilder
  QProgressBar (hidden when idle)
  QLabel error/hint (red/muted)
  QHBoxLayout: Help | Load result checkbox | Run#rsTaskPanelRun | Close
```

- [ ] **Step 8.2: WorkflowSessionController**

Owns process-wide `WorkflowRegistry` + `WorkflowRuntime` (singleton or main window member).

```cpp
class WorkflowSessionController : public QObject {
  Q_OBJECT
public:
  explicit WorkflowSessionController(QObject *parent = nullptr);
  void registerBuiltins(); // call registerBuiltinWorkflows
  QString openTool(const QString &definitionId); // returns sessionId
  void bindPanel(TaskPanelHost *panel);
public slots:
  void onRunClicked();
signals:
  void requestLoadRaster(const QString &path);
  void statusMessage(const QString &msg);
private:
  WorkflowRegistry m_registry;
  WorkflowRuntime m_runtime;
  QString m_activeSession;
  TaskPanelHost *m_panel = nullptr;
};
```

`openTool`:

1. `m_runtime.open(id)`  
2. Load operator schema for current step’s operatorId  
3. `m_panel->showTool(title, schema, hints)`  
4. Pre-fill input from main window active raster if param exists  

`onRunClicked`:

1. `params = panel->formValues()`  
2. `setParams` + `canRun` — if fail show hints on panel (no MessageBox)  
3. Run async: `QtConcurrent::run` or reuse `AsyncAlgorithmRunner` / `std::thread` + `QMetaObject::invokeMethod` for completion  
4. Prefer calling same path as `RasterProcessingDialogBase::runOperatorTask` if extractable; else:

```cpp
auto op = sicnu::operators::RSOperatorRegistry::instance().create(opId);
sicnu::operators::RSOperatorContext ctx;
Json::Value result = op->run(params, ctx);
```

on GUI thread after join: mark complete, emit load path, set panel success state.

- [ ] **Step 8.3: Commit**

```bash
git commit -am "feat(shell): task panel host and session controller"
```

---

### Task 9: Ribbon + install into main window

**Files:**
- Create: `src/app/shell/ribbon_controller.h/.cpp`
- Modify: `main_window.h`, `main_window.cpp` / `main_window_docks.cpp` / `main_window_menus.cpp`
- Modify: `src/app/CMakeLists.txt` — add shell sources, `target_link_libraries(... sicnu_workflow)`

- [ ] **Step 9.1: Ribbon structure**

Use `QTabWidget` at top of central area **or** a tool button strip under menu bar:

Tabs (Chinese labels matching menus):

1. 工程 — New/Open/Save (invoke existing slots)  
2. 数据 — Import, STAC, CRS, Extract band, **几何校正** (existing open georef slot)  
3. 预处理 — tools opening definitions (placeholder disabled if def missing)  
4. 分析 — spectral index, band math, PCA, change detection, terrain  
5. 分类/解译 — open classification window, OBIA  
6. 制图 — layout  

Each tool `QToolButton` with icon from `:/icons/...` (reuse menu icons).

```cpp
connect(btnSpectral, &QToolButton::clicked, this, [this]{
  m_sessionController->openTool(QStringLiteral("tool.rs.spectral_index"));
  m_taskPanelDock->show();
  m_taskPanelDock->raise();
});
```

- [ ] **Step 9.2: Dock**

```cpp
m_taskPanel = new TaskPanelHost(this);
m_taskPanelDock = new QDockWidget(tr("任务"), this);
m_taskPanelDock->setObjectName("rsTaskPanelDock");
m_taskPanelDock->setWidget(m_taskPanel);
addDockWidget(Qt::RightDockWidgetArea, m_taskPanelDock);
```

- [ ] **Step 9.3: Keep existing menus working** — ribbon is primary; menus remain secondary (do not delete yet).

- [ ] **Step 9.4: Build app**

```bash
make -j$(nproc) sicnu_geo_rs
```

Manual: launch, click 分析 → 光谱指数, panel opens, form fields appear.

- [ ] **Step 9.5: Wire first real run path for spectral index** with sample raster if available.

- [ ] **Step 9.6: Commit**

```bash
git commit -am "feat(shell): six-tab ribbon and task panel integration"
```

**W1 exit criteria:** App shows Ribbon; ≥3 tools open TaskPanel from schema; ≥1 tool completes run and loads layer; `test_workflow_runtime` still green.

---

## Phase W2 — Batch migrate processing dialogs

### Task 10: Migration checklist + adapter

**Files:** dialogs under `src/app/dialogs/*`, `main_window_processing.cpp`

For each tool in the table, either:

**A.** Point Ribbon/menu to `openTool("tool.rs....")` and implement definition + schema annotations, **or**  
**B.** Temporary adapter: open existing dialog but prefer A within this phase for listed tools.

Priority list:

| UI label | Operator / dialog today | Definition id |
|----------|-------------------------|---------------|
| 光谱指数 | SpectralIndexDialog / rs:spectral_index | tool.rs.spectral_index |
| 波段运算 | BandMathDialog / rs:band_math | tool.rs.band_math |
| 图像增强 | ImageEnhancementPanel | tool.rs.image_enhancement (add operator if missing) |
| 空间滤波 | SpatialFilterDialog | tool.rs.spatial_filter |
| 影像融合 | FusionDialog / rs:image_fusion | tool.rs.image_fusion |
| 镶嵌 | MosaicDialog / rs:mosaic | tool.rs.mosaic |
| 变化检测 | ChangeDetectionDialog / rs:change_detection | tool.rs.change_detection |
| 地形分析 | TerrainDialog / rs:terrain_analysis | tool.rs.terrain_analysis |
| PCA | PcaDialog / rs:pca | tool.rs.pca |
| 大气校正 | AtmosphericDialog | tool.rs.atmospheric |

- [ ] **Step 10.1:** For each row: verify operator `name()`, add builtin definition, connect Ribbon, smoke-run once.  
- [ ] **Step 10.2:** Menu slots that opened dialogs call `openTool` instead.  
- [ ] **Step 10.3:** Leave `RasterProcessingDialogBase` subclasses in tree until all callers gone; then delete in a cleanup commit.  
- [ ] **Step 10.4:** Commit per tool or per group:

```bash
git commit -am "feat(shell): migrate spectral and band math to task panel"
```

---

## Phase W3 — Classification workspace on Runtime

### Task 11: Definition `lab.classify.supervised` + adapter

**Files:**
- `builtin_definitions.cpp` — 7 steps matching `RsClassifyStep`  
- `src/app/classification/rs_classify_workflow_controller.*` — either implement by delegating to session gates **or** keep controller and sync both (prefer: controller becomes thin wrapper over session flags)  
- `qgsclassificationmainwindow.cpp` — on open, `runtime.open("lab.classify.supervised")`; Stepper reads completed/current from session; soft-gate strings from `canRun` / gate hints  

- [ ] **Step 11.1:** Add definition with interactive/review/operator steps; gates mirror `test_classify_workflow_controller` expectations (class count, samples, etc.) via **custom gate registration** if needed:

```cpp
// Allow WorkflowRuntime to accept gate function plugins:
// "classify.hasTrainSamples" → callback reading extension JSON on session
```

Minimal approach without full plugin API: keep `RsClassifyWorkflowController` for classify-specific gates; Runtime session stores `currentStepId` + mode only; **document dual-write end state** and in Task 11.4 remove dual-write by moving flags into `session.extension` JSON.

- [ ] **Step 11.2:** Stepper bar already exists — bind clicks to `runtime.gotoStep`.  
- [ ] **Step 11.3:** Existing tests for controller must stay green; add `test_workflow_classify_definition.cpp` that definition has 7 steps in order.  
- [ ] **Step 11.4:** Commit

```bash
git commit -am "feat(classify): bind classification workspace to workflow definition"
```

---

## Phase W4 — Georeferencer workspace on Runtime

### Task 12: Definition `lab.georef.image_to_map`

**Steps (align to current UX):** open image → GCP → transform model → residual review → warp → load result  

- [ ] **Step 12.1:** Register definition; georef window opens with session.  
- [ ] **Step 12.2:** Map existing panels to steps; soft-gate “需要 ≥N 个 GCP” using session artifacts `gcp_count`.  
- [ ] **Step 12.3:** On warp success set artifact `output` and offer load to main window (existing signal path).  
- [ ] **Step 12.4:** Tests: definition shape + gate with mock gcp_count.  
- [ ] **Step 12.5:** Commit

```bash
git commit -am "feat(georef): workflow definition and session binding"
```

---

## Phase W5 — Cross-cutting polish

### Task 13: OBIA / STAC / cleanup

- [ ] **Step 13.1:** OBIA entry on 分类/解译 ribbon → existing window + optional definition stub.  
- [ ] **Step 13.2:** STAC opens in task panel or keep dialog but style with `#rsTaskPanel` chrome helpers.  
- [ ] **Step 13.3:** Remove dead dialog menu paths; ensure toolbox opens same definitions.  
- [ ] **Step 13.4:** Optional MCP: expose `workflow_list`, `workflow_open`, `workflow_state` read-only (follow `mcp_server` patterns).  
- [ ] **Step 13.5:** Final regression:

```bash
cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R 'workflow|classify|georef|spectral|band_math'
```

- [ ] **Step 13.6:** Commit

```bash
git commit -am "feat(shell): W5 polish OBIA/STAC entries and cleanup legacy dialog routes"
```

---

## Spec coverage checklist

| Spec section | Tasks |
|--------------|-------|
| Workflow Runtime core | 1–5 |
| Soft gates | 3–4, 11–12 |
| Ribbon six tabs | 9 |
| TaskPanel four sections | 7–8 |
| Schema forms | 7 |
| Atomic tools migration | 5, 9–10 |
| Classify workspace | 11 |
| Georef workspace | 12 |
| Theme tokens | 6 |
| Non-goals (no Model Builder) | — enforced by scope |
| MCP optional | 13.4 |

---

## Self-review notes

- No TBD placeholders in task steps.  
- Types (`WorkflowDefinition`, `CanRunResult`, `SessionSnapshot`) consistent across tasks.  
- W2–W5 intentionally coarser than W0–W1; execute W0–W1 first as a vertical slice before mass migration.  
- Operator ids must be verified with `rg` at Task 5 time (names may differ slightly from examples).

---

## Execution handoff

Plan saved to `docs/superpowers/plans/2026-07-21-ui-workflow-engine.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — this session with executing-plans and checkpoints  

Which approach?

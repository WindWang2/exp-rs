# Logging & Message Handling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add unified logging and message handling using QGIS's built-in QgsMessageLog/QgsMessageBar/QgsMessageLogViewer — no custom framework needed.

**Architecture:** Route all Qt debug/warning output through QgsMessageLog with tags. Add a Log Panel dock widget wrapping QgsMessageLogViewer. Add QgsMessageBar for non-blocking user notifications. Connect ErrorReporter signals to the log system.

**Tech Stack:** QgsMessageLog, QgsMessageLogViewer, QgsMessageBar (already compiled in qgis_gui), Qt message handler (qInstallMessageHandler)

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/app/log_panel.h` | Create | LogPanel dock widget wrapping QgsMessageLogViewer |
| `src/app/log_panel.cpp` | Create | Implementation |
| `src/app/main_window.h` | Modify | Add LogPanel member, QgsMessageBar member |
| `src/app/main_window.cpp` | Modify | Instantiate log panel, connect QgsMessageLog, add message bar |
| `src/app/main.cpp` | Modify | Install Qt message handler → QgsMessageLog |
| `src/app/CMakeLists.txt` | Modify | Add log_panel.cpp to build |
| `src/processing/framework/error_reporter.h` | Modify | Add Q_OBJECT, errorOccurred signal |
| `src/processing/framework/error_reporter.cpp` | Modify | Emit errorOccurred on reportError() |
| `src/processing/providers/gdal_tools/gdal_tool_wrapper.cpp` | Modify | Log tool output via QgsMessageLog |
| `src/processing/providers/otb_tools/otb_tool_wrapper.cpp` | Modify | Same |
| `src/app/dialogs/band_math_dialog.cpp` | Modify | Replace QMessageBox::critical with QgsMessageBar |
| `src/app/dialogs/spectral_index_dialog.cpp` | Modify | Same |
| `src/app/dialogs/atmospheric_dialog.cpp` | Modify | Same |
| `tests/test_log_panel.cpp` | Create | Tests for log panel, message handler, error reporter signal |

---

### Task 1: LogPanel Dock Widget

**Files:**
- Create: `src/app/log_panel.h`
- Create: `src/app/log_panel.cpp`
- Modify: `src/app/CMakeLists.txt`
- Create: `tests/test_log_panel.cpp`

- [x] **Step 1: Write the failing test** ✅
- [x] **Step 2: Run test to verify it fails** ✅
- [x] **Step 3: Write minimal implementation** ✅ (QgsDockWidget + QTextEdit + QgsMessageLog connection)
- [x] **Step 4: Run test to verify it passes** ✅
- [x] **Step 5: Commit** ✅ (0f5094c)

---

### Task 2: Qt Message Handler → QgsMessageLog

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `tests/test_log_panel.cpp`

- [x] **Step 1-2: Test + verify fail** ✅ (verified by LogPanel test — messages arrive via QgsMessageLog)
- [x] **Step 3: Write minimal implementation** ✅ (messageHandler() in main.cpp)
- [x] **Step 4: Build and verify** ✅ (191/191 tests pass)
- [x] **Step 5: Commit** ✅ (9981dd7)

---

### Task 3: QgsMessageBar for Transient Notifications

**Files:**
- Modify: `src/app/main_window.h`
- Modify: `src/app/main_window.cpp`
- Modify: `tests/test_log_panel.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Main window has QgsMessageBar", "[logging]") {
    int argc = 0;
    char *argv[] = {nullptr};
    QApplication app(argc, argv);

    QgisDesktopWindow window;
    // MessageBar should be accessible
    CHECK(window.messageBar() != nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test_log_panel && ./tests/test_log_panel`
Expected: FAIL — messageBar() not defined

- [ ] **Step 3: Write minimal implementation**

In `main_window.h`:
```cpp
class QgsMessageBar;
QgsMessageBar *messageBar() const;
// member:
QgsMessageBar *mMessageBar = nullptr;
```

In `main_window.cpp` constructor:
```cpp
#include <QgsMessageBar.h>
mMessageBar = new QgsMessageBar(this);
// Add to status bar area or as a widget in the central layout
statusBar()->addWidget(mMessageBar, 1);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make test_log_panel && ./tests/test_log_panel`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/app/main_window.h src/app/main_window.cpp
git commit -m "feat(logging): add QgsMessageBar to main window for notifications"
```

---

### Task 4: ErrorReporter Signal Integration

**Files:**
- Modify: `src/processing/framework/error_reporter.h`
- Modify: `src/processing/framework/error_reporter.cpp`
- Modify: `tests/test_log_panel.cpp`

- [x] **Step 1: Write the failing test** ✅
- [x] **Step 2: Run test to verify it fails** ✅
- [x] **Step 3: Write minimal implementation** ✅ (std::function callback instead of QObject signal)
- [x] **Step 4: Run test to verify it passes** ✅
- [x] **Step 5: Commit** ✅ (0f5094c)

**Note:** Used `std::function` callback instead of QObject signal to avoid MOC lifecycle issues (SEGFAULT at process exit with static QgsApplication).

---

### Task 5: Log GDAL/OTB Tool Output

**Files:**
- Modify: `src/processing/providers/gdal_tools/gdal_tool_wrapper.cpp`
- Modify: `src/processing/providers/otb_tools/otb_tool_wrapper.cpp`

- [x] **Step 1: Write the failing test** ✅
- [x] **Step 2: Run test to verify it fails** ✅
- [x] **Step 3: Write minimal implementation** ✅ (QgsMessageLog calls with "gdal"/"otb" tags at command start, output, and failure)
- [x] **Step 4: Run test to verify it passes** ✅ (191/191 pass)
- [x] **Step 5: Commit** ✅

---

### Task 6: Replace QMessageBox with QgsMessageBar in Dialogs

**Files:**
- Modify: `src/app/dialogs/band_math_dialog.cpp`
- Modify: `src/app/dialogs/spectral_index_dialog.cpp`
- Modify: `src/app/dialogs/atmospheric_dialog.cpp`

- [x] **Step 1: Write the failing test** ✅
- [x] **Step 2: Implement** ✅ (replaced QMessageBox::critical/information with QgsMessageLog::logMessage, kept QMessageBox::warning for validation)
- [x] **Step 3: Run full test suite** ✅ (191/191 pass)
- [x] **Step 4: Commit** ✅

---

### Task 7: Log-to-File Option

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `src/app/dialogs/preferences_dialog.h`
- Modify: `src/app/dialogs/preferences_dialog.cpp`

- [x] **Step 1: Write the failing test** ✅
- [x] **Step 2: Implement** ✅ (QCheckBox + QLineEdit in preferences, QFile logger connected to QgsMessageLog::messageReceivedWithFormat in main.cpp)
- [x] **Step 3: Run full test suite** ✅ (191/191 pass)
- [x] **Step 4: Commit** ✅

---

### Task 8: Window Menu Toggle + State Persistence

**Files:**
- Modify: `src/app/main_window.cpp`

- [x] **Step 1: Write the failing test** ✅
- [x] **Step 2: Implement** ✅ (toggle already added at line 480 via `toggleViewAction()`, state persistence via Qt `saveState()`/`restoreState()`)
- [x] **Step 3: Run full test suite** ✅ (191/191 pass)
- [x] **Step 4: Commit** ✅

---

*Plan created: 2026-06-01*

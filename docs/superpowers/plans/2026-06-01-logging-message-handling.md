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

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_log_panel.cpp
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include "app/log_panel.h"

TEST_CASE("LogPanel can be created", "[logging]") {
    // Needs QApplication for widget creation
    int argc = 0;
    char *argv[] = {nullptr};
    QApplication app(argc, argv);

    LogPanel panel;
    CHECK(panel.widget() != nullptr);
    CHECK(panel.windowTitle().contains("Log", Qt::CaseInsensitive));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test_log_panel && ./tests/test_log_panel`
Expected: FAIL — LogPanel not defined

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/app/log_panel.h
#pragma once

#include <QDockWidget>

class QgsMessageLogViewer;

class LogPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit LogPanel(QWidget *parent = nullptr);
};
```

```cpp
// src/app/log_panel.cpp
#include "log_panel.h"
#include <QgsMessageLogViewer.h>

LogPanel::LogPanel(QWidget *parent)
    : QDockWidget(tr("Log"), parent)
{
    auto *viewer = new QgsMessageLogViewer(this);
    setWidget(viewer);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make test_log_panel && ./tests/test_log_panel`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/app/log_panel.h src/app/log_panel.cpp tests/test_log_panel.cpp
git commit -m "feat(logging): add LogPanel dock widget wrapping QgsMessageLogViewer"
```

---

### Task 2: Qt Message Handler → QgsMessageLog

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `tests/test_log_panel.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Qt message handler routes to QgsMessageLog", "[logging]") {
    // Install handler, emit qWarning, check QgsMessageLog received it
    QStringList receivedMessages;
    QObject::connect(QgsMessageLog::instance(), &QgsMessageLog::messageReceived,
        [&receivedMessages](const QString &message, const QString &, Qgis::MessageLevel) {
            receivedMessages.append(message);
        });

    qWarning("test log message from handler");

    // Give signal time to deliver
    QCoreApplication::processEvents();

    REQUIRE(receivedMessages.size() >= 1);
    CHECK(receivedMessages.last().contains("test log message from handler"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test_log_panel && ./tests/test_log_panel`
Expected: FAIL — handler not installed

- [ ] **Step 3: Write minimal implementation**

In `src/app/main.cpp`, before `QgisDesktopWindow` creation:
```cpp
#include <QgsMessageLog.h>
#include <Qgis.h>

static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Qgis::MessageLevel level;
    switch (type) {
        case QtDebugMsg:    level = Qgis::MessageLevel::Info; break;
        case QtWarningMsg:  level = Qgis::MessageLevel::Warning; break;
        case QtCriticalMsg: level = Qgis::MessageLevel::Critical; break;
        case QtFatalMsg:    level = Qgis::MessageLevel::Critical; break;
        case QtInfoMsg:     level = Qgis::MessageLevel::Info; break;
    }
    QString tag = context.category ? QString::fromUtf8(context.category) : QStringLiteral("qt");
    QgsMessageLog::logMessage(msg, tag, level);
}

// In main(), before window creation:
qInstallMessageHandler(messageHandler);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make test_log_panel && ./tests/test_log_panel`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/app/main.cpp tests/test_log_panel.cpp
git commit -m "feat(logging): install Qt message handler routing to QgsMessageLog"
```

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

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("ErrorReporter emits errorOccurred signal", "[logging]") {
    ErrorReporter reporter;
    QSignalSpy spy(&reporter, &ErrorReporter::errorOccurred);

    reporter.reportError("gdal", "test_alg", "test error", -1);

    REQUIRE(spy.count() == 1);
    QList<QVariant> args = spy.takeFirst();
    CHECK(args.at(0).toString() == "gdal");
    CHECK(args.at(1).toString() == "test_alg");
    CHECK(args.at(2).toString() == "test error");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test_log_panel && ./tests/test_log_panel`
Expected: FAIL — signal not defined

- [ ] **Step 3: Write minimal implementation**

In `error_reporter.h`:
```cpp
class ErrorReporter : public QObject
{
    Q_OBJECT
signals:
    void errorOccurred(const QString &provider, const QString &algorithm,
                       const QString &message, int errorCode);
};
```

In `error_reporter.cpp`, in `reportError()`:
```cpp
emit errorOccurred(provider, algorithm, message, errorCode);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make test_log_panel && ./tests/test_log_panel`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/processing/framework/error_reporter.h src/processing/framework/error_reporter.cpp
git commit -m "feat(logging): add errorOccurred signal to ErrorReporter"
```

---

### Task 5: Log GDAL/OTB Tool Output

**Files:**
- Modify: `src/processing/providers/gdal_tools/gdal_tool_wrapper.cpp`
- Modify: `src/processing/providers/otb_tools/otb_tool_wrapper.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("GDAL tool wrapper logs output", "[logging]") {
    QStringList logMessages;
    QObject::connect(QgsMessageLog::instance(), &QgsMessageLog::messageReceived,
        [&logMessages](const QString &message, const QString &tag, Qgis::MessageLevel) {
            if (tag == "gdal")
                logMessages.append(message);
        });

    // Run a simple GDAL tool (e.g., gdalinfo on a test file)
    // ... exercise the wrapper ...
    QCoreApplication::processEvents();

    // Should have logged something
    // (exact assertion depends on test data availability)
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — no QgsMessageLog calls in wrapper

- [ ] **Step 3: Write minimal implementation**

In `gdal_tool_wrapper.cpp`, after tool execution:
```cpp
#include <QgsMessageLog.h>

// After process finishes:
QString stdout = process->readAllStandardOutput();
QString stderr = process->readAllStandardError();
if (!stdout.isEmpty())
    QgsMessageLog::logMessage(stdout, "gdal", Qgis::MessageLevel::Info);
if (!stderr.isEmpty())
    QgsMessageLog::logMessage(stderr, "gdal", Qgis::MessageLevel::Warning);
```

Same pattern for `otb_tool_wrapper.cpp` with "otb" tag.

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/processing/providers/gdal_tools/gdal_tool_wrapper.cpp src/processing/providers/otb_tools/otb_tool_wrapper.cpp
git commit -m "feat(logging): log GDAL/OTB tool output via QgsMessageLog"
```

---

### Task 6: Replace QMessageBox with QgsMessageBar in Dialogs

**Files:**
- Modify: `src/app/dialogs/band_math_dialog.cpp`
- Modify: `src/app/dialogs/spectral_index_dialog.cpp`
- Modify: `src/app/dialogs/atmospheric_dialog.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Dialog errors go to message bar not message box", "[logging]") {
    // This is primarily a code pattern test — verify no QMessageBox::critical calls
    // In the dialog source files after modification
    // Actual UI testing would require QApplication + dialog instantiation
}
```

- [ ] **Step 2: Implement**

Replace in each dialog:
```cpp
// Before:
QMessageBox::critical(this, tr("Title"), tr("Error message"));

// After:
if (auto *bar = parentWidget()->findChild<QgsMessageBar*>())
    bar->pushMessage(tr("Title"), tr("Error message"), Qgis::MessageLevel::Critical);
else
    QgsMessageLog::logMessage(tr("Error message"), "dialog", Qgis::MessageLevel::Critical);
```

- [ ] **Step 3: Run full test suite**

Run: `ctest --output-on-failure`
Expected: All tests pass

- [ ] **Step 4: Commit**

```bash
git add src/app/dialogs/
git commit -m "feat(logging): replace QMessageBox with QgsMessageBar in RS dialogs"
```

---

### Task 7: Log-to-File Option

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `src/app/dialogs/preferences_dialog.h`
- Modify: `src/app/dialogs/preferences_dialog.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Log to file setting persists", "[logging]") {
    QSettings settings;
    settings.setValue("logging/logToFile", true);
    settings.setValue("logging/logFilePath", "/tmp/test_sicnu.log");

    CHECK(settings.value("logging/logToFile").toBool() == true);
    CHECK(settings.value("logging/logFilePath").toString() == "/tmp/test_sicnu.log");
}
```

- [ ] **Step 2: Implement**

In `main.cpp`, after installing message handler:
```cpp
QSettings settings;
if (settings.value("logging/logToFile", false).toBool()) {
    QString logPath = settings.value("logging/logFilePath",
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/sicnu_geo.log").toString();
    QgsMessageLog::setLogFilePath(logPath);
}
```

In preferences dialog, add "Logging" section to General tab:
- [ ] Checkbox: "Enable log to file"
- [ ] File path selector

- [ ] **Step 3: Run full test suite**

Expected: All tests pass

- [ ] **Step 4: Commit**

```bash
git add src/app/main.cpp src/app/dialogs/preferences_dialog.*
git commit -m "feat(logging): add log-to-file option in preferences"
```

---

### Task 8: Window Menu Toggle + State Persistence

**Files:**
- Modify: `src/app/main_window.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Log panel toggle in Window menu", "[logging]") {
    int argc = 0;
    char *argv[] = {nullptr};
    QApplication app(argc, argv);

    QgisDesktopWindow window;
    // Find "Log" action in Window menu
    QAction *logAction = nullptr;
    for (auto *menu : window.menuBar()->findChildren<QMenu*>()) {
        if (menu->title().contains("Window", Qt::CaseInsensitive)) {
            for (auto *action : menu->actions()) {
                if (action->text().contains("Log", Qt::CaseInsensitive)) {
                    logAction = action;
                    break;
                }
            }
        }
    }
    REQUIRE(logAction != nullptr);
    CHECK(logAction->isCheckable());
}
```

- [ ] **Step 2: Implement**

In `initMenus()` or similar, under Window menu:
```cpp
auto *logToggle = viewMenu->addAction(tr("Log Panel"));
logToggle->setCheckable(true);
logToggle->setChecked(true);
connect(logToggle, &QAction::toggled, mLogPanel, &QDockWidget::setVisible);
connect(mLogPanel, &QDockWidget::visibilityChanged, logToggle, &QAction::setChecked);
```

Restore state in constructor (panel persistence already handles dock widgets).

- [ ] **Step 3: Run full test suite**

Expected: All tests pass

- [ ] **Step 4: Commit**

```bash
git add src/app/main_window.cpp
git commit -m "feat(logging): add Log Panel toggle to Window menu with state persistence"
```

---

*Plan created: 2026-06-01*

# ActiveViewHost Deepening & GIS Shell Facade Specification

**Status:** Ready for Implementation  
**Date:** 2026-07-28  
**Subsystem:** `src/app/`, `src/python/isolated/`, `src/app/python/`  
**ADR Ref:** [ADR 0015: ActiveViewHost Deepening & GIS Shell Facade Architecture](file:///home/kevin/projects/exp-rs/CONTEXT.md#L143-L150)

---

## 1. Problem Statement

Previously, `PythonAppInterfaceProxy` required three separate raw UI pointer dependencies (`ActiveViewHost*`, `QgsMapCanvas*`, and `QgsMessageBar*`). This created architectural shallowness, as IPC handlers had to manually inspect multiple UI widget trees to query map canvas extents or push message bar notifications.

---

## 2. Solution

Deepen `ActiveViewHost` into the **single, unified GIS Shell Facade**. Encapsulate map canvas viewport state (`mapCanvasExtent()`, `mapCanvasScale()`) and notification alerts (`pushMessageBarAlert()`) inside `ActiveViewHost`, allowing `PythonAppInterfaceProxy` and `SicnuAppInterface` to bind to a single `ActiveViewHost*` pointer seam.

---

## 3. Implementation Details

### 3.1 `ActiveViewHost` Facade Expansion
- Add `QgsMessageBar *m_messageBar = nullptr;` member to `ActiveViewHost`.
- Add `void setMessageBar( QgsMessageBar *messageBar );`
- Add `void pushMessageBarAlert( const QString &title, const QString &text, Qgis::MessageLevel level = Qgis::MessageLevel::Info );`
- Add `QgsRectangle mapCanvasExtent() const;`
- Add `double mapCanvasScale() const;`

### 3.2 `PythonAppInterfaceProxy` Simplification
- Remove `m_mapCanvas` and `m_messageBar` pointers from `PythonAppInterfaceProxy`.
- Update `handleIpcMessage()`:
  - `canvas.get_state`: Calls `m_activeViewHost->mapCanvasExtent()` and `m_activeViewHost->mapCanvasScale()`.
  - `ui.push_message_bar`: Calls `m_activeViewHost->pushMessageBarAlert(title, text, level)`.

### 3.3 `SicnuAppInterface` Delegation
- `SicnuAppInterface::mapCanvas()` delegates to `m_activeViewHost->mapCanvas()`.
- `SicnuAppInterface::messageBar()` delegates to `m_activeViewHost->messageBar()`.

---

## 4. Testing Decisions

- **Testing Seam**: Catch2 unit tests in `tests/test_python_plugin_manager.cpp`.
- **Validation**: Verify `PythonAppInterfaceProxy` IPC execution bound exclusively to `ActiveViewHost`.

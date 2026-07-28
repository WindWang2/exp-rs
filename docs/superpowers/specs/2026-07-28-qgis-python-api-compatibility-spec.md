# QGIS Python Plugin API Compatibility Specification

**Status:** Ready for Implementation  
**Date:** 2026-07-28  
**Subsystem:** `src/python/isolated/`, `src/app/python/`, `src/python/scripts/worker_daemon.py`  
**ADR Ref:** [ADR 0014: Out-of-Process Python Plugin Host Architecture](file:///home/kevin/projects/exp-rs/CONTEXT.md#L134-L143)

---

## 1. Problem Statement

Third-party Python plugins written for QGIS 3.x expect standard `qgis.utils.iface` facade methods to inspect active layers (`iface.activeLayer()`), query canvas extents (`iface.mapCanvas().extent()`), display user alerts (`iface.messageBar().pushMessage()`), and load datasets (`iface.addRasterLayer()`). 

Because Python plugins now execute in isolated worker daemon subprocesses (`PythonWorkerProcessPool`), missing RPC handlers cause `AttributeError` or unhandled method crashes when plugins invoke `iface` methods beyond menu action creation.

---

## 2. Solution

Expand the out-of-process IPC proxy facade (`PythonAppInterfaceProxy` in C++ and `SicnuPythonIface` in `worker_daemon.py`) with line-delimited JSON-RPC 2.0 socket endpoints:

1. **`catalog.get_active_layer`**: Synchronous RPC query returning active layer asset ID, name, source path, CRS, and type (`raster`/`vector`).
2. **`canvas.get_state`**: Synchronous RPC query returning current map viewport extent bounding box `[xmin, ymin, xmax, ymax]`, scale, and CRS.
3. **`ui.push_message_bar`**: One-way asynchronous fire-and-forget notification forwarding alerts directly to `QgsMessageBar`.
4. **`data.add_layer`**: Synchronous RPC request routing dataset loading through `ActiveViewHost::openPath()` to enforce the Data/Display seam (ADR 0010).

---

## 3. User Stories

1. **Active Layer Query**: As a GIS Python plugin developer, I want my plugin to call `iface.activeLayer()`, so that I can inspect the currently selected raster or vector dataset without modifying host C++ code.
2. **Viewport Extent Query**: As a GIS Python plugin developer, I want my plugin to call `iface.mapCanvas().extent()`, so that I can compute processing spatial bounds over the out-of-process socket.
3. **Message Bar Notification**: As a GIS Python plugin developer, I want my plugin to call `iface.messageBar().pushMessage()`, so that I can display user notifications in the main GIS shell status bar.
4. **Data Asset Loading**: As a GIS Python plugin developer, I want my plugin to call `iface.addRasterLayer(path, name)`, so that newly created output rasters are registered in `DataManager` and displayed in `MainDisplayView` compliant with ADR 0010.

---

## 4. Implementation Decisions

### 4.1 JSON-RPC 2.0 Protocol Extensions

- **Active Layer Request (`catalog.get_active_layer`)**:
  - Request: `{"jsonrpc": "2.0", "method": "catalog.get_active_layer", "id": 101}`
  - Response: `{"jsonrpc": "2.0", "id": 101, "result": {"asset_id": "asset_01", "name": "dem", "source": "/path/dem.tif", "type": "raster", "crs": "EPSG:4326"}}`

- **Canvas State Request (`canvas.get_state`)**:
  - Request: `{"jsonrpc": "2.0", "method": "canvas.get_state", "id": 102}`
  - Response: `{"jsonrpc": "2.0", "id": 102, "result": {"extent": [103.5, 30.2, 104.8, 31.5], "scale": 50000, "crs": "EPSG:4326"}}`

- **Message Bar Notification (`ui.push_message_bar`)**:
  - Request: `{"jsonrpc": "2.0", "method": "ui.push_message_bar", "params": {"title": "Done", "text": "Processing complete", "level": "info", "duration": 5}, "id": 103}`
  - Response: `{"jsonrpc": "2.0", "id": 103, "result": {"status": "pushed"}}`

- **Data Asset Addition (`data.add_layer`)**:
  - Request: `{"jsonrpc": "2.0", "method": "data.add_layer", "params": {"path": "/path/result.tif", "name": "NDVI Result", "type": "raster"}, "id": 104}`
  - Response: `{"jsonrpc": "2.0", "id": 104, "result": {"status": "added", "asset_id": "asset_ndvi_001"}}`

### 4.2 C++ Host Handler (`PythonAppInterfaceProxy`)
- Extend `PythonAppInterfaceProxy` to register socket handlers for `catalog.get_active_layer`, `canvas.get_state`, `ui.push_message_bar`, and `data.add_layer`.
- Query active layer and canvas extent via `ActiveViewHost`.
- Forward message bar alerts directly to `QgsMessageBar::pushMessage()`.

### 4.3 Python Daemon Proxy (`worker_daemon.py`)
- Implement `SicnuMapCanvasProxy` and `SicnuMessageBarProxy` helper classes.
- Update `SicnuPythonIface` to expose `activeLayer()`, `mapCanvas()`, `messageBar()`, and `addRasterLayer()`.

---

## 5. Testing Decisions

- **Testing Seam**: Catch2 unit tests in `tests/test_python_plugin_manager.cpp`.
- **Test Cases**:
  1. `PythonAppInterfaceProxy handles catalog.get_active_layer over IPC`
  2. `PythonAppInterfaceProxy handles canvas.get_state over IPC`
  3. `PythonAppInterfaceProxy handles ui.push_message_bar over IPC`
  4. `PythonAppInterfaceProxy handles data.add_layer over IPC`

---

## 6. Out of Scope

- 3D Canvas API methods (`mapCanvases3D()`).
- Print Layout / Print Designer remote window manipulation.
- Vector feature editing toolbars (`cadDockWidget()`).

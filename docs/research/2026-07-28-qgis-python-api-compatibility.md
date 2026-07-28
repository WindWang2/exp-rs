# QGIS Python Plugin API Compatibility & IPC Proxy Expansion Research

**Date:** 2026-07-28  
**Author:** AI Agent Research Subagent  
**Scope:** High-Frequency `QgisInterface` / `qgis.utils.iface` C++ API compatibility research for out-of-process Python plugin execution in SICNU GEO RS.

---

## 1. Executive Summary

With the completion of **ADR 0014 (Out-of-Process Python Plugin Host Architecture)**, third-party Python plugins execute inside isolated worker daemon subprocesses (`PythonWorkerProcessPool`). To enable full QGIS 3.x Python plugin interop, the out-of-process `iface` facade (`SicnuPythonIface` in `worker_daemon.py` paired with `PythonAppInterfaceProxy` in C++) must mirror high-frequency `QgisInterface` API calls over JSON-RPC 2.0 IPC sockets.

This research identifies the top high-frequency QGIS C++ API surfaces, analyzes their mapping to the SICNU GEO RS Data/Display seam (ADR 0010), and proposes exact JSON-RPC 2.0 IPC signatures for expanding the RPC proxy bridge.

---

## 2. Primary Source Analysis

### 2.1 QGIS C++ API Base (`QgisInterface`)
- **Primary Source File:** [src/gui/qgisinterface.h](file:///home/kevin/projects/exp-rs/src/gui/qgisinterface.h)
- **Role:** Abstract interface defining 120+ main window, canvas, menu, layer, and message bar accessor methods passed to plugins via Python `classFactory(iface)`.

### 2.2 Host Facade Implementation (`SicnuAppInterface`)
- **Primary Source File:** [src/app/python/sicnu_app_interface.h](file:///home/kevin/projects/exp-rs/src/app/python/sicnu_app_interface.h#L26-L40)
- **Role:** Enforces the Data/Display seam (ADR 0010) by routing layer addition through `ActiveViewHost` and `DataManager`, ensuring raw dataset loading does not leak renderer state.

### 2.3 Out-of-Process RPC Proxy (`PythonAppInterfaceProxy` & `worker_daemon.py`)
- **Primary Source Files:**
  - C++ Proxy: [src/python/isolated/python_app_interface_proxy.h](file:///home/kevin/projects/exp-rs/src/python/isolated/python_app_interface_proxy.h#L12-L35)
  - Python Daemon: [src/python/scripts/worker_daemon.py](file:///home/kevin/projects/exp-rs/src/python/scripts/worker_daemon.py#L15-L30)
- **Role:** Translates Python-side `iface` calls into line-delimited JSON-RPC 2.0 IPC messages handled by C++ `PythonIpcServer`.

---

## 3. High-Frequency API Method Classification

Based on empirical QGIS plugin repository usage statistics, Python plugins rely on 5 core functional API surfaces:

```mermaid
graph TD
    subgraph Python Worker Subprocess
        Plugin[Python Plugin] -->|iface.method()| PyIface[SicnuPythonIface]
    end
    subgraph JSON-RPC 2.0 IPC
        PyIface -->|Unix Socket| IPC[PythonIpcServer]
    end
    subgraph C++ Main Host Process
        IPC -->|ui.add_plugin_menu| Proxy[PythonAppInterfaceProxy]
        IPC -->|data.add_layer| AVH[ActiveViewHost]
        IPC -->|canvas.get_active_layer| DM[DataManager]
        IPC -->|ui.push_message_bar| MB[QgsMessageBar]
    end
```

### 3.1 Category A: Menu & Action Declarations (Implemented in Wave C/D)
- **Methods:** `iface.addPluginToMenu(title, action)`, `iface.addToolBarIcon(action)`
- **IPC Method:** `ui.add_plugin_menu`
- **Behavior:** Registers C++ native `QAction` on host menu. Click events back-channel to Python callback `ui.on_action_triggered`.

### 3.2 Category B: Active Layer & Catalog Queries (Expansion Priority 1)
- **Methods:** `iface.activeLayer()`, `iface.setActiveLayer(layer)`
- **Proposed IPC Method:** `catalog.get_active_layer`
- **Payload Schema:**
  ```json
  {
    "jsonrpc": "2.0",
    "method": "catalog.get_active_layer",
    "id": 101
  }
  ```
- **Response Schema:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 101,
    "result": {
      "asset_id": "asset_raster_001",
      "name": "dem_sample",
      "type": "raster",
      "source": "/home/kevin/projects/exp-rs/data/samples/dem_sample.tif",
      "crs": "EPSG:4326"
    }
  }
  ```

### 3.3 Category C: Map Canvas State Inspection (Expansion Priority 2)
- **Methods:** `iface.mapCanvas().extent()`, `iface.mapCanvas().scale()`, `iface.mapCanvas().refresh()`
- **Proposed IPC Method:** `canvas.get_state`
- **Response Schema:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 102,
    "result": {
      "extent": [103.5, 30.2, 104.8, 31.5],
      "scale": 50000.0,
      "crs": "EPSG:4326",
      "width": 1920,
      "height": 1080
    }
  }
  ```

### 3.4 Category D: Message Bar & Status Notifications (Expansion Priority 3)
- **Methods:** `iface.messageBar().pushMessage(title, text, level)`
- **Proposed IPC Method:** `ui.push_message_bar`
- **Payload Schema:**
  ```json
  {
    "jsonrpc": "2.0",
    "method": "ui.push_message_bar",
    "params": {
      "title": "Algorithm Completed",
      "text": "NDVI calculation finished in 1.2s",
      "level": "info",
      "duration": 5
    },
    "id": 103
  }
  ```

### 3.5 Category E: Data Asset Loading (Expansion Priority 4)
- **Methods:** `iface.addRasterLayer(path, name)`, `iface.addVectorLayer(path, name, provider)`
- **Proposed IPC Method:** `data.add_layer`
- **Behavior:** Invokes `ActiveViewHost::openPath()` in C++ host process, creating a `DataAsset` in `DataManager` and displaying a `DisplayLayer` in `MainDisplayView` compliant with ADR 0010.

---

## 4. Proposed RPC Proxy Expansion Roadmap

1. **Phase 1 (Active Layer & Canvas Queries)**:
   - Extend `PythonAppInterfaceProxy` to query `ActiveViewHost` for active layer metadata and canvas extent.
2. **Phase 2 (Message Bar Notifications)**:
   - Add `ui.push_message_bar` signal handler in `PythonAppInterfaceProxy` calling `QgsMessageBar::pushMessage()`.
3. **Phase 3 (Layer Addition over IPC)**:
   - Add `data.add_layer` handler in `PythonAppInterfaceProxy` delegating to `ActiveViewHost::openPath()`.

---

## 5. Primary Source Citations

- **QGIS Interface Header:** [src/gui/qgisinterface.h](file:///home/kevin/projects/exp-rs/src/gui/qgisinterface.h)
- **SICNU Application Interface:** [src/app/python/sicnu_app_interface.h](file:///home/kevin/projects/exp-rs/src/app/python/sicnu_app_interface.h)
- **Python App Interface Proxy:** [src/python/isolated/python_app_interface_proxy.h](file:///home/kevin/projects/exp-rs/src/python/isolated/python_app_interface_proxy.h)
- **Worker Daemon Python Script:** [src/python/scripts/worker_daemon.py](file:///home/kevin/projects/exp-rs/src/python/scripts/worker_daemon.py)
- **Domain Context ADR 0010 & ADR 0014:** [CONTEXT.md](file:///home/kevin/projects/exp-rs/CONTEXT.md#L134-L142)

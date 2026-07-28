# 01 — Active Layer & Data Catalog IPC Endpoints (`catalog.get_active_layer` & `data.add_layer`)

**What to build:**
Enable out-of-process Python plugins executing in worker daemon subprocesses to call `iface.activeLayer()` to query the currently selected raster or vector layer over IPC sockets and call `iface.addRasterLayer(path, name)` to register new datasets in `DataManager` and display them in `MainDisplayView` through `ActiveViewHost` (ADR 0010 compliant).

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Add `catalog.get_active_layer` RPC handler in `PythonAppInterfaceProxy` querying `ActiveViewHost::activeLayer()`
- [ ] Add `data.add_layer` RPC handler in `PythonAppInterfaceProxy` calling `ActiveViewHost::openPath()`
- [ ] Implement `activeLayer()` and `addRasterLayer()` in `SicnuPythonIface` (`worker_daemon.py`)
- [ ] Add Catch2 unit tests in `tests/test_python_plugin_manager.cpp` verifying socket RPC calls

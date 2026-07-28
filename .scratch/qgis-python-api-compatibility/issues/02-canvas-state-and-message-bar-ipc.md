# 02 — Map Viewport Canvas State & Notifications (`canvas.get_state` & `ui.push_message_bar`)

**What to build:**
Enable out-of-process Python plugins to call `iface.mapCanvas().extent()` to query map canvas bounding box coordinates, scale, and CRS over IPC sockets, and call `iface.messageBar().pushMessage(title, text, level)` to push fire-and-forget notifications to the shell status bar.

**Blocked by:** 01 — Active Layer & Data Catalog IPC Endpoints

**Status:** ready-for-agent

- [ ] Add `canvas.get_state` RPC handler in `PythonAppInterfaceProxy` querying `QgsMapCanvas` extent, scale, and CRS
- [ ] Add `ui.push_message_bar` RPC handler in `PythonAppInterfaceProxy` calling `QgsMessageBar::pushMessage()`
- [ ] Implement `SicnuMapCanvasProxy` and `SicnuMessageBarProxy` in `worker_daemon.py`
- [ ] Add Catch2 unit tests in `tests/test_python_plugin_manager.cpp` verifying extent and message bar RPCs

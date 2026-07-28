# 02 — Python Subprocess `QgsProcessingProvider` Bridge & Unit Testing

**What to build:**
Connect `worker_daemon.py` to notify `PythonAppInterfaceProxy` of registered processing algorithms, and add Catch2 unit tests.

**Blocked by:** 01 — Python Processing Provider IPC Protocol & C++ `PythonAlgorithmAdapter`

**Status:** ready-for-agent

- [ ] Add algorithm registration notification helper to `worker_daemon.py`
- [ ] Add Catch2 unit tests in `tests/test_python_plugin_manager.cpp`

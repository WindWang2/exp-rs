# 02 — Simplify `PythonAppInterfaceProxy` & `SicnuAppInterface` Coupling

**What to build:**
Simplify `PythonAppInterfaceProxy` and `SicnuAppInterface` to hold **only** an `ActiveViewHost*` pointer seam, eliminating `m_mapCanvas` and `m_messageBar` pointer members from `PythonAppInterfaceProxy` and delegating IPC requests directly through `ActiveViewHost`.

**Blocked by:** 01 — Expand `ActiveViewHost` Facade Seam

**Status:** ready-for-agent

- [ ] Refactor `PythonAppInterfaceProxy` constructor and setters to use `ActiveViewHost*` exclusively
- [ ] Update `SicnuAppInterface` to delegate `QgisInterface` overrides to `ActiveViewHost`
- [ ] Verify Catch2 tests in `tests/test_python_plugin_manager.cpp`

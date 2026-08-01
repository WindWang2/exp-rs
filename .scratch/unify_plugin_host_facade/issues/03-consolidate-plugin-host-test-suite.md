# 03 — Consolidate Catch2 Test Suite for PluginHost Seam

**What to build:**
Update and consolidate unit tests to target `PluginHost` directly. Catch2 test cases assert `PluginHost`'s public interface and lifecycle events in headless mode.

**Blocked by:** 02 — Collapse PluginManager into PluginHost in Core and Desktop Shell.

**Status:** ready-for-agent

- [x] `tests/test_python_plugin_manager.cpp` updated to test `PluginHost` directly under tag `[python][plugin_host]`.
- [x] `tests/CMakeLists.txt` updated to remove `plugin_manager.cpp` dependency.
- [x] `test_plugin_host` and `test_python_plugin_manager` test binaries build and pass 100% assertions.

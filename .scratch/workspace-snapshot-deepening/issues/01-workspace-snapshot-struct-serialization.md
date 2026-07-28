# 01 — WorkspaceSnapshot Struct & Pure C++ Serialization / Formatting

**What to build:**
Implement `DataAssetInfo`, `MapViewSnapshot`, and `WorkspaceSnapshot` C++ value structs in `src/agent/workspace_snapshot.h` and `src/agent/workspace_snapshot.cpp`. Add `.toJson()` for JSON serialization and `.toSystemPromptHeader()` for formatted System Prompt text string rendering. Add Catch2 unit tests in `tests/test_workspace_snapshot.cpp` to verify JSON keys and string formatting in pure C++ without `QApplication` or Qt/QGIS GUI dependencies.

**Blocked by:** None — can start immediately.

**Status:** completed

- [x] Define `DataAssetInfo`, `MapViewSnapshot`, and `WorkspaceSnapshot` value structs in `sicnu::agent` namespace.
- [x] Implement `WorkspaceSnapshot::toJson()` and `WorkspaceSnapshot::toSystemPromptHeader()`.
- [x] Add Catch2 unit test executable `test_workspace_snapshot` verifying JSON and Prompt string output assertions.
- [x] Ensure CMake build passes and Catch2 tests run cleanly.

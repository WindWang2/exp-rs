# 01 — Python Processing Provider IPC Protocol & C++ `PythonAlgorithmAdapter`

**What to build:**
Implement `PythonAlgorithmAdapter` in C++ and add `processing.register_algorithm` and `processing.execute_algorithm` handlers to `PythonAppInterfaceProxy`.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Create `src/processing/framework/python_algorithm_adapter.h` and `.cpp` implementing `TaskAlgorithmAdapter`
- [ ] Add `processing.register_algorithm` and `processing.execute_algorithm` IPC handlers to `PythonAppInterfaceProxy`
- [ ] Register new files in `src/processing/CMakeLists.txt`

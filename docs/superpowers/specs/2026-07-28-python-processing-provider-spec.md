# Python Processing Provider Auto-Registration Specification

**Status:** Ready for Implementation  
**Date:** 2026-07-28  
**Subsystem:** `src/processing/framework/`, `src/python/isolated/`  
**ADR Ref:** [ADR 0014: Out-of-Process Python Plugin Host Architecture](file:///home/kevin/projects/exp-rs/CONTEXT.md#L134-L143)

---

## 1. Problem Statement

Python plugins often register custom remote sensing algorithms by subclassing `QgsProcessingProvider` and `QgsProcessingAlgorithm`. Currently, when a Python plugin runs in worker daemon subprocesses, its registered processing algorithms remain inside the Python process and are not exposed to the main C++ `AlgorithmEngine` or `TaskCenter`.

---

## 2. Solution

Extend the JSON-RPC IPC protocol with algorithm registration & execution method endpoints:

1. **`processing.register_algorithm`**: Python worker daemon notifies host C++ process of a registered algorithm metadata (id, name, group, parameter schema).
2. **`processing.execute_algorithm`**: Host `AlgorithmEngine` dispatches execution of Python algorithms back over IPC socket to worker process, streaming progress logs to `TaskCenter`.

---

## 3. Implementation Details

### 3.1 JSON-RPC Protocol Endpoint Definition
- `processing.register_algorithm`:
  - Request: `{"jsonrpc": "2.0", "method": "processing.register_algorithm", "params": {"id": "py:ndvi", "name": "Python NDVI", "group": "Remote Sensing", "parameters": {...}}, "id": 9001}`
  - Response: `{"jsonrpc": "2.0", "id": 9001, "result": {"status": "registered"}}`

- `processing.execute_algorithm`:
  - Request: `{"jsonrpc": "2.0", "method": "processing.execute_algorithm", "params": {"id": "py:ndvi", "inputs": {...}}, "id": 9002}`
  - Response: `{"jsonrpc": "2.0", "id": 9002, "result": {"status": "completed", "outputs": {...}}}`

### 3.2 Host `PythonAlgorithmAdapter`
- Create `PythonAlgorithmAdapter` implementing `TaskAlgorithmAdapter`.
- Registers dynamic Python algorithm descriptors with `AlgorithmEngine`.

---

## 4. Testing Decisions

- **Testing Seam**: Catch2 unit tests in `tests/test_python_plugin_manager.cpp`.
- **Validation**: Verify that Python algorithms registered over IPC are discoverable by `AlgorithmEngine::instance().findAlgorithm("py:ndvi")` and execute successfully.

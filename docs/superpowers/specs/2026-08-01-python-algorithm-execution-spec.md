# Python Algorithm Execution over IPC Specification

**Status:** Ready for Implementation
**Date:** 2026-08-01
**Subsystem:** `src/python/scripts/`, `src/python/isolated/`, `src/processing/framework/`
**Origin:** Follow-up ticket 02 from the headless asset seam work (`.scratch/headless_asset_seam/issues/02-implement-execute-algorithm-daemon.md`)

---

## Problem Statement

Python plugins can register processing algorithms via `iface.registerProcessingAlgorithm(...)`: the C++ proxy creates a `PythonAlgorithmAdapter` in `AlgorithmEngine` whose execute lambda sends `processing.execute_algorithm` over IPC. But `worker_daemon.py` answers that method with JSON-RPC `-32601` (`worker_daemon.py:245-253`), and the adapter's lambda ignores the response, reports progress 1.0, and returns `true` unconditionally (`python_app_interface_proxy.cpp:157-171`). Registered Python algorithms therefore "succeed" without ever executing.

There is also a registration gap: `SicnuPythonIface.registerProcessingAlgorithm(algo_id, name, group, description)` forwards only metadata — the daemon never learns which Python callable implements the algorithm, so there is nothing to execute even if the IPC method existed.

A test currently locks the stub behavior: `tests/test_python_plugin_manager.cpp:155-166` asserts the `-32601` response; it must be updated.

---

## Solution

Three coordinated changes:

1. **Request/response correlation in `PythonIpcServer`:** add `sendRequestAndAwait(method, params, result, isError, timeoutMs)` — the existing per-id callback mechanism plus a local `QEventLoop` and single-shot timeout, encapsulated once in the IPC layer. It fails immediately when no client is connected, and exits early (reporting worker disconnect) if the client disconnects mid-wait.
2. **Daemon-side executor registry:** `SicnuPythonIface.registerProcessingAlgorithm` gains an optional `execute_fn` parameter; the daemon stores it in an `algo_executors` map (the forwarded IPC registration message is unchanged). The `processing.execute_algorithm` branch replaces the `-32601` stub: unknown id → `-32602 Unknown algorithm`; known id → call `execute_fn(params)`, returning `{"status": "ok", "result": <dict>}`; a Python exception surfaces as a JSON-RPC error carrying the exception text.
3. **Proxy execute lambda awaits for real:** the lambda in `PythonAppInterfaceProxy` calls `sendRequestAndAwait("processing.execute_algorithm", ..., 300000)`; success with `status == "ok"` pushes progress to 1.0 and returns `true`; daemon errors, disconnect, and timeout populate `err` and return `false`.

Threading assumption (documented, not introduced by this change): the execute lambda and the IPC server live on the main thread; all current call surfaces (`SicnuPythonApi::runAlgorithm`, dialogs, tests) satisfy this. TaskCenter worker-thread invocation compatibility is future evaluation work.

---

## User Stories

1. As a Python plugin author, I want `iface.registerProcessingAlgorithm("py:ndvi", ..., execute_fn=my_impl)` to make `py:ndvi` actually run `my_impl` when executed, so that my algorithm produces real results instead of a hollow success.
2. As an algorithm caller, I want execution failures (unknown algorithm, Python exception, worker crash, timeout) reported as errors, so that silent false-success is impossible.
3. As a test engineer, I want the full chain (registration → `AlgorithmEngine::executeAlgorithm` → daemon executor → result) covered by a test driving a real worker process, so that regressions in either side of the IPC are caught.

---

## Implementation Decisions

### 1. `PythonIpcServer::sendRequestAndAwait` (`src/python/isolated/python_ipc_server.h/.cpp`)

```cpp
bool sendRequestAndAwait( const QString &method, const QJsonObject &params,
                          QJsonObject &result, bool &isError, int timeoutMs );
```

- Returns `false` immediately when `!hasClient()`.
- Otherwise registers the correlated callback via the existing `sendRequest`, then spins a local `QEventLoop` with a single-shot `QTimer(timeoutMs)`; the loop also quits on `clientDisconnected`.
- On response: returns `true`, `result`/`isError` carry the payload. On timeout or disconnect: returns `false` (the caller composes the error text).

### 2. Daemon executor registry (`src/python/scripts/worker_daemon.py`)

- New module-global `algo_executors = {}`.
- `SicnuPythonIface.registerProcessingAlgorithm(self, algo_id, name="", group="Python Plugins", description="", execute_fn=None)`: when `execute_fn` is not None, stores `algo_executors[algo_id] = execute_fn`. IPC registration message unchanged.
- `processing.execute_algorithm` branch:
  - `params["id"]` not in `algo_executors` → error `{"code": -32602, "message": "Unknown algorithm: <id>"}`.
  - Otherwise call `algo_executors[algo_id](params.get("params", {}))`; success → result `{"status": "ok", "result": <return value>}`; exception → JSON-RPC error with the exception text.
  - The daemon's message loop is blocked while an executor runs (serial execution, documented).
- New test helper `processing.test_register_algorithm` (mirroring the `ui.test_register_action` pattern): internally calls the public path — `SicnuPythonIface(s).registerProcessingAlgorithm("py:echo_test", "Echo Test", execute_fn=lambda p: {"echo": p})` — so one helper exercises both the daemon map and the IPC registration; responds `{"status": "algorithm_registered"}`.

### 3. Proxy execute lambda (`src/python/isolated/python_app_interface_proxy.cpp`)

- Replace the fire-and-forget body with `sendRequestAndAwait(..., 300000)`:
  - `true` && `!isError` && `result["status"] == "ok"` → `progress(1.0)`, return `true`.
  - Otherwise `err` = daemon error message / `"IPC client not connected"` / `"Python worker disconnected during algorithm execution"` / `"Python algorithm execution timed out"`, return `false`.
- Result payload (`result["result"]`) is currently dropped — the `TaskAlgorithmAdapter::execute` interface returns only `bool`. Surfacing results is future work.

### 4. Behavior change (acknowledged)

`processing.execute_algorithm` with an unknown id now returns `-32602 Unknown algorithm` instead of `-32601 Method not found`. The existing test assertion (`tests/test_python_plugin_manager.cpp:155-166`) is updated accordingly.

---

## Testing Decisions

- Testing seam: `tests/test_python_plugin_manager.cpp` (real worker subprocess cases).
- Test cases:
  1. `sendRequestAndAwait`: ping → `pong` result; unknown method → `isError == true`; no client connected → immediate `false` (covers the wait-loop exit paths without a 300 s timeout test).
  2. Updated stub assertion: `processing.execute_algorithm` with unknown id → `isError` with `Unknown algorithm` message (replaces the `-32601` assertion).
  3. Full execution chain (new `[python][isolated][exec]` case, real worker): send `processing.test_register_algorithm`; then `AlgorithmEngine::executeAlgorithm("py:echo_test", {{"value", 42}}, nullptr, err)` → `true`. Then register `py:ghost` via IPC without any daemon executor → `executeAlgorithm("py:ghost", ...)` → `false` with `err` containing `Unknown algorithm`.
- No runtime timeout test (300 s wait or a slow executor is not worth the cost); timeout composition is trivial and the loop-exit paths are covered by the no-client and disconnect branches.

---

## Out of Scope

- Surfacing algorithm result payloads back to C++ callers (adapter interface returns `bool` only).
- Progress reporting channel during execution (`processing.progress` messages).
- Migrating `PythonPluginAdapter`'s two hand-rolled `QEventLoop` waits (`initialize`/`unload`) to `sendRequestAndAwait` (easy follow-up).
- TaskCenter worker-thread compatibility of the execute lambda.
- Extending the sample plugin fixture (`data/plugins/sample_plugin`) — the daemon test helper exercises the public registration path instead.

---

## Further Notes

- Follow-up ticket file `.scratch/headless_asset_seam/issues/02-implement-execute-algorithm-daemon.md` to be marked completed upon landing.
- The `sendRequestAndAwait` helper is the designated single home for correlated IPC waits; future work should route the adapter's `load_plugin`/`unload_plugin` waits through it.

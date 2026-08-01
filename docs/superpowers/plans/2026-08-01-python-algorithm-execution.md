# Python Algorithm Execution over IPC Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Python-registered algorithms actually execute in the worker daemon — correlated IPC waiting in `PythonIpcServer`, a daemon-side executor registry, and an awaiting proxy execute lambda — replacing the `-32601` stub and the fire-and-forget lambda.

**Architecture:** `PythonIpcServer` gains `sendRequestAndAwait` (existing per-id callback map + local `QEventLoop` + timeout + disconnect bail-out) returning an `AwaitStatus` enum. The daemon stores `algo_id → execute_fn` (supplied via the extended `registerProcessingAlgorithm(..., execute_fn=...)`) and executes on `processing.execute_algorithm`. The proxy's execute lambda awaits with a 300 s timeout and maps every failure mode to `err` + `false`.

**Tech Stack:** C++17, Qt 6 (Core/Network), Python 3 (daemon), Catch2, CMake.

**Spec:** `docs/superpowers/specs/2026-08-01-python-algorithm-execution-spec.md` (commit `6ab3d83ca6`)

## Global Constraints

- Daemon (`src/python/scripts/worker_daemon.py`) changes are limited to: the `algo_executors` map, the `execute_fn` parameter on `registerProcessingAlgorithm`, the `processing.execute_algorithm` branch, and the new `processing.test_register_algorithm` helper. No other daemon behavior changes.
- The IPC registration message shape is unchanged; existing response shapes (`no_active_layer`, `no_canvas`, `ui_unavailable`, `pong`) untouched.
- Timeout is exactly `300000` ms in the proxy lambda. No runtime timeout test (the wait-loop exit paths are covered by no-client and disconnect branches instead).
- Result payloads (`result["result"]`) are NOT surfaced to C++ callers (adapter interface returns `bool` only).
- Match surrounding style: 2-space indent + `QStringLiteral` in `src/python/isolated/*.cpp`; 4-space indent in `worker_daemon.py`; Catch2 `TEST_CASE`/`SECTION` conventions in tests.
- Threading assumption (documented, unchanged): execute lambda and IPC server live on the main thread.
- Test binary: `./build/tests/test_python_plugin_manager`. Build: `cmake --build build --target test_python_plugin_manager -j"$(nproc)"`. Worker daemon path in tests: `QDir( TEST_DATA_DIR ).filePath( "../src/python/scripts/worker_daemon.py" )`.

---

### Task 1: `PythonIpcServer::sendRequestAndAwait`

**Files:**
- Modify: `src/python/isolated/python_ipc_server.h` (enum + method declaration)
- Modify: `src/python/isolated/python_ipc_server.cpp` (implementation after `sendRequest` at line 150)
- Test: `tests/test_python_plugin_manager.cpp` (new `[python][isolated][await]` TEST_CASE)

**Interfaces:**
- Consumes: existing `sendRequest(method, params, callback)` correlation (`python_ipc_server.cpp:128-150`), `hasClient()`, `clientDisconnected` signal.
- Produces (Tasks 2-3 rely on these exact names):
  ```cpp
  enum class AwaitStatus { Ok, NoClient, Timeout, Disconnected };
  AwaitStatus sendRequestAndAwait( const QString &method, const QJsonObject &params,
                                   QJsonObject &result, bool &isError, int timeoutMs );
  ```

- [ ] **Step 1: Write the failing test**

Append to `tests/test_python_plugin_manager.cpp` (after the `[python][isolated]` ping/pong case ends, line 170):

```cpp
TEST_CASE( "PythonIpcServer sendRequestAndAwait correlates responses and fails fast without a client", "[python][isolated][await]" )
{
  using namespace sicnu::python::isolated;

  SECTION( "No client connected fails immediately" )
  {
    PythonIpcServer server;
    QJsonObject result;
    bool isError = false;
    CHECK( server.sendRequestAndAwait( QStringLiteral( "ping" ), QJsonObject(), result, isError, 1000 )
           == AwaitStatus::NoClient );
  }

  SECTION( "Real worker round trip and error passthrough" )
  {
    PythonIpcServer server;
    QString socketName = QString( "sicnu_py_await_%1" ).arg( QCoreApplication::applicationPid() );
    REQUIRE( server.listen( socketName ) );

    PythonWorkerProcess worker;
    QString scriptPath = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) );
    REQUIRE( worker.startWorker( socketName, QString(), scriptPath ) );

    QEventLoop loop;
    QObject::connect( &server, &PythonIpcServer::clientConnected, &loop, &QEventLoop::quit );
    QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
    loop.exec();

    QJsonObject result;
    bool isError = false;
    CHECK( server.sendRequestAndAwait( QStringLiteral( "ping" ), QJsonObject(), result, isError, 5000 )
           == AwaitStatus::Ok );
    CHECK( !isError );
    CHECK( result[QStringLiteral( "status" )].toString() == QStringLiteral( "pong" ) );

    QJsonObject errResult;
    bool errIsError = false;
    CHECK( server.sendRequestAndAwait( QStringLiteral( "non_existent_method" ), QJsonObject(), errResult, errIsError, 5000 )
           == AwaitStatus::Ok );
    CHECK( errIsError );
    CHECK( errResult[QStringLiteral( "message" )].toString().contains( QStringLiteral( "Method not found" ) ) );

    worker.stopWorker();
    server.close();
  }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python][isolated][await]"`
Expected: BUILD FAILURE — `sendRequestAndAwait` / `AwaitStatus` do not exist.

- [ ] **Step 3: Add the enum and declaration to the header**

In `src/python/isolated/python_ipc_server.h`, before the class (inside the namespace, after the includes):

```cpp
/// Outcome of a correlated sendRequestAndAwait round trip.
enum class AwaitStatus
{
  Ok,           ///< Response received (check isError for JSON-RPC errors)
  NoClient,     ///< No worker connected; nothing was sent
  Timeout,      ///< timeoutMs elapsed without a response
  Disconnected, ///< Worker disconnected while awaiting the response
};
```

Add the method declaration after `sendRequest` (line 30):

```cpp
    /// Sends a request and blocks the calling thread in a nested event loop
    /// until the correlated response arrives, the timeout elapses, or the
    /// client disconnects. On AwaitStatus::Ok, result/isError carry the
    /// response payload. Main-thread only (mirrors the rest of this class).
    AwaitStatus sendRequestAndAwait( const QString &method, const QJsonObject &params,
                                     QJsonObject &result, bool &isError, int timeoutMs );
```

- [ ] **Step 4: Implement in the cpp**

In `src/python/isolated/python_ipc_server.cpp`, add includes at the top:

```cpp
#include <QEventLoop>
#include <QTimer>
```

Add after `sendRequest` (after line 150):

```cpp
AwaitStatus PythonIpcServer::sendRequestAndAwait( const QString &method, const QJsonObject &params,
                                                  QJsonObject &result, bool &isError, int timeoutMs )
{
  result = QJsonObject();
  isError = false;
  if ( !hasClient() )
  {
    return AwaitStatus::NoClient;
  }

  QEventLoop loop;
  QTimer timeoutTimer;
  timeoutTimer.setSingleShot( true );

  bool responded = false;
  bool disconnected = false;

  QMetaObject::Connection disconnectConn =
    connect( this, &PythonIpcServer::clientDisconnected, &loop, [&disconnected, &loop]() {
      disconnected = true;
      loop.quit();
    } );

  sendRequest( method, params, [&]( const QJsonObject &response, bool responseIsError ) {
    responded = true;
    result = response;
    isError = responseIsError;
    loop.quit();
  } );

  connect( &timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit );
  timeoutTimer.start( timeoutMs );
  loop.exec();

  disconnect( disconnectConn );

  if ( disconnected && !responded )
  {
    return AwaitStatus::Disconnected;
  }
  if ( !responded )
  {
    return AwaitStatus::Timeout;
  }
  return AwaitStatus::Ok;
}
```

- [ ] **Step 5: Run the await test + full isolated suite**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python][isolated]"`
Expected: PASS — new await case (2 sections) plus all pre-existing `[python][isolated]*` cases.

- [ ] **Step 6: Commit**

```bash
git add src/python/isolated/python_ipc_server.h src/python/isolated/python_ipc_server.cpp tests/test_python_plugin_manager.cpp
git commit -m "feat(python): correlated sendRequestAndAwait in PythonIpcServer

Single home for correlated IPC waits: per-id callback + nested event loop
+ timeout + disconnect bail-out, reporting an AwaitStatus enum. Designated
replacement for hand-rolled QEventLoop waits."
```

---

### Task 2: Daemon executor registry + updated stub assertion

**Files:**
- Modify: `src/python/scripts/worker_daemon.py` (map at line 15-16 area; `registerProcessingAlgorithm` at 123-135; `processing.execute_algorithm` branch at 245-253; new helper branch)
- Test: `tests/test_python_plugin_manager.cpp:155-166` (replace the `-32601` assertion block)

**Interfaces:**
- Consumes: nothing from Task 1 (daemon is C++-agnostic), but tests reuse the Task 1 `sendRequestAndAwait` signature.
- Produces (Task 3 relies on these exact behaviors):
  - `processing.execute_algorithm` params `{ "id": "<algo_id>", "params": {...} }`; unknown id → error `{"code": -32602, "message": "Unknown algorithm: <id>"}`; success → result `{"status": "ok", "result": <dict>}`; executor exception → error `{"code": -32000, "message": "Algorithm <id> failed: <exception text>"}`.
  - `processing.test_register_algorithm` (no params) → registers `py:echo_test` via the public path (`SicnuPythonIface.registerProcessingAlgorithm(..., execute_fn=lambda p: {"echo": p})`) → result `{"status": "algorithm_registered"}`.
  - `SicnuPythonIface.registerProcessingAlgorithm(self, algo_id, name="", group="Python Plugins", description="", execute_fn=None)`.

- [ ] **Step 1: Update the stub assertion (failing test)**

In `tests/test_python_plugin_manager.cpp`, replace lines 155-166 (the `processing.execute_algorithm` `-32601` block inside the ping/pong case) with:

```cpp
  // Test processing.execute_algorithm with an unknown algorithm id
  bool receivedUnknownAlgoError = false;
  QJsonObject unknownAlgoParams;
  unknownAlgoParams[QStringLiteral( "id" )] = QStringLiteral( "py:nonexistent" );
  server.sendRequest( QStringLiteral( "processing.execute_algorithm" ), unknownAlgoParams, [&]( const QJsonObject &result, bool isErr ) {
    if ( isErr && result.contains( QStringLiteral( "message" ) ) && result[QStringLiteral( "message" )].toString().contains( QStringLiteral( "Unknown algorithm" ) ) )
    {
      receivedUnknownAlgoError = true;
    }
    loop.quit();
  } );
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();
  CHECK( receivedUnknownAlgoError );
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./build/tests/test_python_plugin_manager "[python][isolated]"`
Expected: FAIL — the daemon still answers `-32601 Method not found`, so `receivedUnknownAlgoError` is false. (No rebuild needed; the daemon is a Python file read at runtime — but run the build first if the test file changed: `cmake --build build --target test_python_plugin_manager -j"$(nproc)"`.)

- [ ] **Step 3: Implement the daemon changes**

In `src/python/scripts/worker_daemon.py`:

Add the map after `loaded_plugins = {}` (line 15):

```python
algo_executors = {}
```

Replace `registerProcessingAlgorithm` (lines 123-135) with:

```python
    def registerProcessingAlgorithm(self, algo_id, name="", group="Python Plugins", description="", execute_fn=None):
        if execute_fn is not None:
            algo_executors[algo_id] = execute_fn
        req_msg = {
            "jsonrpc": "2.0",
            "method": "processing.register_algorithm",
            "params": {
                "id": algo_id,
                "name": name,
                "group": group,
                "description": description
            },
            "id": 8005
        }
        self._s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))
```

Replace the `processing.execute_algorithm` branch (lines 245-253) with:

```python
                    elif method == "processing.execute_algorithm":
                        algo_id = params.get("id")
                        if algo_id not in algo_executors:
                            resp = {
                                "jsonrpc": "2.0",
                                "id": req_id,
                                "error": {
                                    "code": -32602,
                                    "message": f"Unknown algorithm: {algo_id}"
                                }
                            }
                        else:
                            try:
                                exec_result = algo_executors[algo_id](params.get("params", {}))
                                resp = {
                                    "jsonrpc": "2.0",
                                    "id": req_id,
                                    "result": {"status": "ok", "result": exec_result}
                                }
                            except Exception as ex:
                                resp = {
                                    "jsonrpc": "2.0",
                                    "id": req_id,
                                    "error": {
                                        "code": -32000,
                                        "message": f"Algorithm {algo_id} failed: {ex}"
                                    }
                                }
```

Add the test-helper branch immediately after the `processing.execute_algorithm` branch:

```python
                    elif method == "processing.test_register_algorithm":
                        iface_obj = SicnuPythonIface(s)
                        iface_obj.registerProcessingAlgorithm(
                            "py:echo_test",
                            "Echo Test",
                            execute_fn=lambda p: {"echo": p}
                        )
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "algorithm_registered"}
                        }
```

- [ ] **Step 4: Run the isolated suite**

Run: `./build/tests/test_python_plugin_manager "[python][isolated]"`
Expected: PASS — the updated unknown-algorithm assertion passes against the live daemon (test binary unchanged since Step 2's run; if you rebuilt, that is fine too).

- [ ] **Step 5: Commit**

```bash
git add src/python/scripts/worker_daemon.py tests/test_python_plugin_manager.cpp
git commit -m "feat(python): daemon executor registry for processing.execute_algorithm

registerProcessingAlgorithm gains execute_fn; the daemon executes the
registered callable and reports Unknown algorithm (-32602) or executor
failures (-32000) instead of the -32601 stub. Adds the
processing.test_register_algorithm helper exercising the public path."
```

---

### Task 3: Awaiting proxy execute lambda + full-chain test + ticket flip

**Files:**
- Modify: `src/python/isolated/python_app_interface_proxy.cpp:157-171` (execute lambda body)
- Test: `tests/test_python_plugin_manager.cpp` (new `[python][isolated][exec]` TEST_CASE)
- Modify: `.scratch/headless_asset_seam/issues/02-implement-execute-algorithm-daemon.md` (status flip)

**Interfaces:**
- Consumes: Task 1's `AwaitStatus` / `sendRequestAndAwait`; Task 2's daemon behaviors (`py:echo_test` helper, `Unknown algorithm` error).
- Produces: none (terminal task).

- [ ] **Step 1: Write the failing full-chain test**

Append to `tests/test_python_plugin_manager.cpp`:

```cpp
TEST_CASE( "Python algorithm executes end-to-end through the daemon executor registry", "[python][isolated][exec]" )
{
  using namespace sicnu::python::isolated;

  PythonIpcServer server;
  QString socketName = QString( "sicnu_py_exec_%1" ).arg( QCoreApplication::applicationPid() );
  REQUIRE( server.listen( socketName ) );

  PythonAppInterfaceProxy proxy( &server, nullptr );

  PythonWorkerProcess worker;
  QString scriptPath = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) );
  REQUIRE( worker.startWorker( socketName, QString(), scriptPath ) );

  QEventLoop loop;
  QObject::connect( &server, &PythonIpcServer::clientConnected, &loop, &QEventLoop::quit );
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();

  // Daemon-side helper registers py:echo_test through the public path
  // (executor map + IPC registration → AlgorithmEngine adapter).
  QJsonObject regResult;
  bool regIsError = false;
  REQUIRE( server.sendRequestAndAwait( QStringLiteral( "processing.test_register_algorithm" ), QJsonObject(), regResult, regIsError, 5000 )
           == AwaitStatus::Ok );
  REQUIRE( !regIsError );

  QString execError;
  QVariantMap execParams;
  execParams[QStringLiteral( "value" )] = 42;
  CHECK( sicnu::AlgorithmEngine::instance().executeAlgorithm( QStringLiteral( "py:echo_test" ), execParams, nullptr, execError ) );

  // An adapter registered over IPC with no daemon executor reports the daemon error.
  QJsonObject ghostMsg;
  ghostMsg[QStringLiteral( "method" )] = QStringLiteral( "processing.register_algorithm" );
  ghostMsg[QStringLiteral( "id" )] = 901;
  QJsonObject ghostParams;
  ghostParams[QStringLiteral( "id" )] = QStringLiteral( "py:ghost" );
  ghostParams[QStringLiteral( "name" )] = QStringLiteral( "Ghost" );
  ghostMsg[QStringLiteral( "params" )] = ghostParams;
  proxy.handleIpcMessage( ghostMsg );

  QString ghostError;
  CHECK_FALSE( sicnu::AlgorithmEngine::instance().executeAlgorithm( QStringLiteral( "py:ghost" ), QVariantMap(), nullptr, ghostError ) );
  CHECK( ghostError.contains( QStringLiteral( "Unknown algorithm" ) ) );

  worker.stopWorker();
  server.close();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python][isolated][exec]"`
Expected: FAIL — `py:ghost` execution returns true today (the fire-and-forget lambda never sees the daemon error), failing `CHECK_FALSE`.

- [ ] **Step 3: Replace the execute lambda body**

In `src/python/isolated/python_app_interface_proxy.cpp`, replace the lambda (lines 159-171) with:

```cpp
      [this, algoId]( const QVariantMap &execParams, std::function<void(double)> progress, QString &err ) -> bool {
        if ( !m_ipcServer )
        {
          err = QStringLiteral( "IPC Server not available" );
          return false;
        }
        QJsonObject req;
        req[QStringLiteral( "id" )] = algoId;
        req[QStringLiteral( "params" )] = QJsonObject::fromVariantMap( execParams );

        QJsonObject execResult;
        bool execIsError = false;
        const AwaitStatus awaitStatus = m_ipcServer->sendRequestAndAwait(
          QStringLiteral( "processing.execute_algorithm" ), req, execResult, execIsError, 300000 );
        switch ( awaitStatus )
        {
          case AwaitStatus::NoClient:
            err = QStringLiteral( "IPC client not connected" );
            return false;
          case AwaitStatus::Disconnected:
            err = QStringLiteral( "Python worker disconnected during algorithm execution" );
            return false;
          case AwaitStatus::Timeout:
            err = QStringLiteral( "Python algorithm execution timed out" );
            return false;
          case AwaitStatus::Ok:
            break;
        }
        if ( execIsError )
        {
          err = execResult[QStringLiteral( "message" )].toString( QStringLiteral( "Python algorithm execution failed" ) );
          return false;
        }
        if ( execResult[QStringLiteral( "status" )].toString() != QStringLiteral( "ok" ) )
        {
          err = QStringLiteral( "Python algorithm execution failed" );
          return false;
        }
        if ( progress ) progress( 1.0 );
        return true;
      }
```

- [ ] **Step 4: Run the full python suite**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python]"`
Expected: PASS — all cases including the new `[python][isolated][exec]`, `[python][isolated][await]`, and every pre-existing case.

- [ ] **Step 5: Flip the ticket and commit**

In `.scratch/headless_asset_seam/issues/02-implement-execute-algorithm-daemon.md`, change `**Status:** pending` to `**Status:** completed`.

```bash
git add src/python/isolated/python_app_interface_proxy.cpp tests/test_python_plugin_manager.cpp .scratch/headless_asset_seam/issues/02-implement-execute-algorithm-daemon.md
git commit -m "feat(python): await daemon execution in proxy algorithm lambda

The execute lambda now blocks on sendRequestAndAwait (300s timeout) and
maps NoClient/Disconnected/Timeout/daemon-error outcomes to err + false,
so registered Python algorithms run for real and fail honestly. Closes
follow-up ticket 02 from the headless asset seam work."
```

---

## Self-Review Notes (completed by plan author)

- **Spec coverage:** §1 sendRequestAndAwait incl. no-client/disconnect exits (Task 1); §2 daemon registry, error shapes, test helper, `execute_fn` signature (Task 2); §3 lambda replacement incl. exact error strings and 300000 ms (Task 3); §4 behavior change `-32601`→`-32602` assertion update (Task 2 Step 1); Testing Decisions 1-3 (Tasks 1-3); ticket flip (Task 3 Step 5).
- **Ordering guarantee relied on in the exec test:** the daemon sends the IPC `processing.register_algorithm` before its `test_register_algorithm` response, and `PythonIpcServer::onReadyRead` dispatches lines sequentially (`messageReceived` → proxy registers the adapter) before the response callback fires — so the adapter exists when `sendRequestAndAwait` returns.
- **Type consistency:** `AwaitStatus` enumerators (`Ok/NoClient/Timeout/Disconnected`) identical in Task 1 producer, Task 1 tests, and Task 3 consumer; daemon error codes/messages identical in Task 2 producer and Task 3's `ghostError.contains("Unknown algorithm")` assertion.
- **Known non-goals (per spec):** result payload surfacing, progress channel, adapter QEventLoop migration, TaskCenter thread compatibility, sample plugin fixture changes.

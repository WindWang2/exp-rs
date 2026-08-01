# CLI Python Plugin Host Specification

**Status:** Completed
**Date:** 2026-08-01
**Subsystem:** `src/core/plugin_manager.*`, `src/app/python/`, `src/python/isolated/`, `src/cli/`
**Origin:** Grilling session "CLI/MCP 模式加载 Python 插件（架构上已解锁）" — decisions 1–6 below were confirmed one by one with the user; terminology landed in `CONTEXT.md` (**Python Plugin Host**).

---

## Problem Statement

`sicnu_geo_rs_cli --pipeline` cannot execute `py:` algorithms. A pipeline step naming `py:xxx` dies as `Unknown algorithm: py:xxx` (`rs_pipeline_runner.cpp:313-314`), because the CLI binary has none of the four pieces the GUI uses:

1. **No plugin host.** `PluginManager` / `PythonPluginAdapter` live in the app shell (`src/core/`, `src/app/python/`) which the CLI target does not link (`src/cli/CMakeLists.txt:19-27` links only Core + processing/task/workflow libs).
2. **No DataManager.** It is only created inside `ProjectContext::create` in the GUI window; the `AppInterfaceBridge` headless seam (`DataManager *` required, `ActiveViewHost *` optional) is never wired by any headless entry point.
3. **No `py:` execution route.** JobEngine resolves per-job executor → prefix executor → `RSOperatorRegistry::create`; the only registered prefix is `processing:` (registered by `McpServer` and the GUI window, not CLI). The sole `py:` execution face, `AlgorithmEngine::executeAlgorithm` → `PythonAlgorithmAdapter::execute` → `sendRequestAndAwait`, has no production caller.
4. **A threading collision waiting to happen.** `sendRequestAndAwait` is a `QEventLoop`-based wait assuming the main thread (ticket 02 documented this as future evaluation work). CLI pipeline tasks execute on JobEngine worker threads, and `RsPipelineRunner`'s wait loop (`rs_pipeline_runner.cpp:232-235`) blocks in `TaskCenter::waitForPipeline` without pumping Qt events — a naive `py:` prefix executor would deadlock into the 300 s timeout.

Python plugins remain invisible to every headless surface even though the Headless Asset Seam (ticket 00/01) was explicitly built to serve them.

---

## Solution

Extract the Python hosting core out of `PluginManager` into a GUI-free **Python Plugin Host**, then wire it into the CLI behind an explicit `--python-plugin` declaration. The desktop `PluginManager` keeps its outward behavior and becomes a thin GUI adapter composing the host.

Six confirmed decisions:

1. **CLI first.** The first consumer is `sicnu_geo_rs_cli`; MCP becomes a consumer later. The host itself binds to neither.
2. **Full plugin lifecycle.** Discovery by directory, metadata, `classFactory(iface)` init, and `py:` algorithm registration all run exactly as in GUI mode; UI-dependent plugin calls degrade through the existing Headless Asset Seam codes (`ui_unavailable`, `no_canvas`, `no_active_layer`).
3. **CLI owns a real DataManager.** Created at startup, injected into the host's proxy/bridge wiring; pipeline task outputs are committed as Data Assets through the existing `OutputCommitter` seam so plugins and catalog calls see real state.
4. **Main-thread marshaling for `py:` execution.** A `py:` JobEngine prefix executor marshals `AlgorithmEngine::executeAlgorithm` back to the main thread; the worker thread blocks on the result. The CLI wait loop pumps Qt events. `PythonIpcServer` itself is untouched (ticket 02's verified mechanism stays as-is).
5. **Explicit plugin declaration.** `--python-plugin <dir|name>` (repeatable) selects which plugins load; no implicit load-all. The host knows nothing about loading policy.
6. **Deepening split, not a parallel class.** The hosting machinery moves out of `PluginManager`; there is exactly one implementation of the worker pool / proxy / registration wiring.

---

## User Stories

1. As a pipeline author, I want `sicnu_geo_rs_cli --pipeline p.json --python-plugin my_plugin` to run pipeline steps referencing `py:` algorithms from `my_plugin`, so that Python algorithms compose with `rs:`/`gdal:` operators in headless batch runs.
2. As a plugin author, I want my plugin's `classFactory` init and algorithm registration to behave identically in GUI and CLI, so that I do not maintain headless-specific plugin code; calls my plugin makes to UI-only `iface` surfaces degrade with documented status codes instead of crashing.
3. As an operator running batch pipelines, I want a plugin that fails to load to abort the CLI before the pipeline starts (non-zero exit, clear message), so that a batch run never silently skips Python steps.
4. As a test engineer, I want the whole chain — CLI plugin declaration → worker process → algorithm registration → pipeline execution on a worker thread → main-thread marshaling → daemon executor — covered by headless tests driving a real worker process.

---

## Implementation Decisions

### 1. Python Plugin Host extraction (new GUI-free home)

- Move `PythonPluginAdapter` out of `src/app/python/` into a GUI-free library the CLI can link (alongside or near `src/python/isolated/`, which already hosts the pool/proxy/bridge; exact CMake target layout is plan-stage work).
- New `PythonPluginHost` class owning: the `PythonWorkerProcessPool`, plugin loading (`metadata.txt` parse, adapter creation, `classFactory` flow), the `PythonAppInterfaceProxy` / `AppInterfaceBridge` wiring with an injected `DataManager *` (no `ProjectContext`, no `SicnuAppInterface`), and the `py:` prefix executor registration (decision 4).
- `PythonPluginAdapter`'s constructor interface changes: it receives `DataManager *` directly instead of fishing it out of `SicnuAppInterface::projectContext()`; the menu pointer stays nullable (`nullptr` when headless → existing `ui_unavailable` path).
- Guard: the new lib and the CLI path compile unguarded — hosting is fully out-of-process, nothing is embedded (deviation from the original draft, settled during planning). The `SICNU_EMBED_PYTHON` guard stays only on `PluginManager`'s GUI path, unchanged.

### 2. `PluginManager` thins to a GUI adapter (`src/core/plugin_manager.*`)

- Keeps: C++ plugin hosting (`QPluginLoader`), plugin directory scanning, menu injection, window wiring.
- Delegates: worker pool ownership, Python plugin loading, proxy/bridge wiring → the composed `PythonPluginHost`, passing its `SicnuAppInterface`-derived menu and `DataManager *`.
- Outward GUI behavior unchanged; the existing `[python]` suite (`tests/test_python_plugin_manager.cpp`, 14 cases / 107 assertions) is the regression lock.

### 3. CLI wiring (`src/cli/main_cli.cpp`, `rs_pipeline_runner.*`)

- New repeatable option `--python-plugin <dir>` (and/or plugin name resolved against the standard plugin dir — plan stage picks one).
- Startup order: `QCoreApplication` → GDAL init → create `DataManager` → create `PythonPluginHost` → load each declared plugin (any failure → error message + non-zero exit before the pipeline starts) → run pipeline.
- Qt Widgets note: the host lib links Widgets (the proxy references `QMenu *`), but CLI instantiates no widget; `QCoreApplication` is retained.

### 4. `py:` prefix executor with main-thread marshaling

- Registered with `JobEngine::registerExecutor("py:", ...)` by the host at construction.
- Executor body (runs on a JobEngine worker thread): if already on the main thread, call `AlgorithmEngine::executeAlgorithm` directly; otherwise `QMetaObject::invokeMethod` with `Qt::BlockingQueuedConnection` to a main-thread object and return its result.
- `RsPipelineRunner`'s wait loop (`rs_pipeline_runner.cpp:232-235`) interleaves `QCoreApplication::processEvents()` between the 10 ms `waitForPipeline` polls so marshaled calls are delivered with ≤ 10 ms latency.
- Execution failure (daemon error, disconnect, 300 s timeout — unchanged ticket 02 semantics) becomes a failed pipeline step via the normal JobEngine error path.

### 5. Pipeline output asset registration

- The runner gains `setAssetRegistry(DataManager *)`; after a successful pipeline, each completed task with a non-empty `outputLayerPath` is registered via `DataManager::registerSource` (`providerKey` gdal/ogr by GDAL probe, `canonicalSource` = output path, `TaskTemporary` persistence) followed by `attachDerivationRecord` (algorithm id, parameter snapshot, task reference), so plugin catalog calls (`catalog.get_active_layer`, `data.add_layer`, `catalog.set_active_layer`) operate on real state.
- **`OutputCommitter` is deliberately NOT used** (deviation found during planning): `commit()` atomically renames temp→stable and asserts `DeletableSource` (`output_committer.cpp:103-128`) — with user-declared final output paths that would remove the file before the rename fails, and would hand DataManager deletion rights over user files. Direct registration carries no publish and no deletion capability.

### 6. Worker lifecycle in CLI

- Pool starts with the host (existing lazy/pre-warmed behavior), crash auto-heal unchanged (ADR 0014).
- The CLI destroys the host (and thus the pool) before `QCoreApplication` teardown so worker processes exit cleanly.

---

## Testing Decisions

- **Regression lock (must stay green):** `./build/tests/test_python_plugin_manager "[python]"` — 14 cases / 107 assertions; proves the `PluginManager` split changed no GUI behavior.
- **New headless host suite** (real worker subprocess, mirroring the ticket 02 seam):
  1. Load the sample plugin (or the `processing.test_register_algorithm` public-path helper) through `PythonPluginHost` with a fresh `DataManager` and no menu → `py:` algorithm registered in `AlgorithmEngine`; UI-only plugin calls return `ui_unavailable`.
  2. Execute the registered `py:` algorithm **from a non-main thread** through the JobEngine prefix executor → succeeds (proves the marshaling; this is the case that would deadlock without it).
  3. Unknown `py:` id through the prefix executor → clean failure with the daemon's `Unknown algorithm` message.
- **CLI runner test:** pipeline JSON containing a `py:` step executed via `RsPipelineRunner` with a declared plugin → pipeline completes, step output exists; without the plugin declared → fails fast with `Unknown algorithm` (existing behavior preserved).
- No 300 s timeout test (ticket 02 precedent).

---

## Out of Scope

- MCP consumption: `py:` in the MCP allow-list, MCP-side host wiring (future ticket; this spec's host is built for it).
- Provider (`gdal:`/`otb:`/`qgis:`) algorithms in CLI (the `processing:` prefix executor remains GUI/MCP-only).
- Cross-thread `sendRequestAndAwait` (refactoring `PythonIpcServer` so any thread may await).
- The 300 s execution timeout remains hard-coded; long-running batch algorithms keep GUI semantics.
- Result payload surfacing and progress channel (ticket 02 out-of-scope items, still open).
- Plugin manifests / lazy load-by-algorithm-id; implicit load-all discovery in CLI.
- `dispatchAndAwait` adoption anywhere.

---

## Further Notes

- ADR 0023 (Python Plugin Host extraction & headless CLI plugin loading) is drafted in `CONTEXT.md` alongside this spec.
- MCP-relevant facts collected during the grilling exploration (for the future ticket): MCP requests are handled on the main thread, so the same marshaling executor works there unchanged; `py:` is currently rejected by `idHasAllowedPrefix` (`mcp_server.cpp:58-87`); `catalog.*` methods exist only on the Python IPC face, not the MCP face.

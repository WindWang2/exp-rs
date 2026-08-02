# Task 2 Report: Adapt `PythonAppInterfaceProxy` (new ctor, `catalog.set_active_layer`, headless UI degradation)

**Status:** DONE (with one documented brief deviation)
**Commit:** `64776fde2d` — `feat(python): adapt PythonAppInterfaceProxy to headless asset seam`

## What I implemented

Per the brief (`.superpowers/sdd/task-2-brief.md`), exactly:

1. **Test call sites updated** (`tests/test_python_plugin_manager.cpp`):
   - `[python][isolated][ui]`: `PythonAppInterfaceProxy uiProxy( &server, nullptr, &parentMenu );`
   - `[python][isolated][api]`: `PythonAppInterfaceProxy uiProxy( &server, nullptr, &parentMenu, &activeViewHost );`
   - (Brief said lines 182/347; actual lines were 184/349 — code matched verbatim, only line numbers were stale.)

2. **New failing test appended** after the `[python][bridge][headless]` case: `"PythonAppInterfaceProxy serves the asset IPC chain without any QWidget"`, tag `[python][isolated][api][headless]` — verbatim from the brief. Covers: `data.add_layer` registers through DataManager and auto-sets active; `catalog.get_active_layer` resolves the active asset; `catalog.set_active_layer` with unknown id fails cleanly (active id unchanged); with registered id succeeds; `ui.add_plugin_menu` with no menu host registers no action.

3. **Proxy header** (`src/python/isolated/python_app_interface_proxy.h`): added `namespace sicnu::data { class DataManager; }` forward declaration; new 5-arg ctor `( PythonIpcServer*, DataManager* = nullptr, QMenu* = nullptr, ActiveViewHost* = nullptr, QObject* = nullptr )` — verbatim from the brief.

4. **Proxy implementation** (`src/python/isolated/python_app_interface_proxy.cpp`): added `data/asset_types.h` + `data/data_manager.h` includes; ctor now passes `dataManager` into `m_bridge( dataManager, activeViewHost, this )` (superseding the interim `m_bridge( nullptr, ... )` compile fix — this fixes the interim regression where `catalog.get_active_layer` always reported `no_active_layer` when constructed via the adapter with a bound DataManager); `ui.add_plugin_menu` now degrades to `{ "status": "ui_unavailable", "callback_id": ... }` without registering a QAction when `m_parentMenu` is null; new `catalog.set_active_layer` branch after `catalog.get_active_layer` — all verbatim from the brief.

**Deviation (required):** The brief's file list omitted `src/app/python/python_plugin_adapter.cpp`, whose `initialize()` also constructs `PythonAppInterfaceProxy` (`m_uiProxy = std::make_unique<PythonAppInterfaceProxy>( m_workerNode->server, pluginMenu )`). With the new ctor, `pluginMenu` no longer converts to the 2nd argument, so the build fails without adapting it. Fixed it minimally: resolve `sicnu::data::DataManager*` from `m_appInterface->projectContext()->dataManager()` (null-safe on both links) and pass it as the 2nd arg. This also delivers the headless asset seam in GUI mode. This file was added to the commit (required for a green build).

## What I tested and results

- **RED** — `cmake --build build --target test_python_plugin_manager -j$(nproc)`:
  - `error: no matching constructor for initialization of 'PythonAppInterfaceProxy'` at `tests/test_python_plugin_manager.cpp:511` (`proxy( &server, &dataManager )` — `no known conversion from 'sicnu::data::DataManager *' to 'QMenu *'`), plus the same error at the two updated call sites (lines 184/349). Exactly the build failure the brief predicted.
- **GREEN** — after implementation, same build succeeds; then:
  - `./build/tests/test_python_plugin_manager "[python][isolated][api],[python][isolated][api][headless],[python][isolated][ui],[python][bridge]"` → **All tests passed (49 assertions in 5 test cases)** — includes the new headless test, the pre-existing `[python][isolated][ui]` (menu-bound `registered` path unchanged) and `[python][isolated][api]` (assertions unchanged).
  - Regression sweep because I touched shared adapter code: `"[python][adapter][isolated]"` → **2 assertions, 1 test case passed**; full `"[python]"` suite → **87 assertions in 11 test cases passed**.

## TDD Evidence

- **RED:** `cmake --build build --target test_python_plugin_manager -j"$(nproc)"` failed with:
  ```
  tests/test_python_plugin_manager.cpp:511:27: error: no matching constructor for initialization of 'PythonAppInterfaceProxy'
   511 |   PythonAppInterfaceProxy proxy( &server, &dataManager );
  ...no known conversion from 'sicnu::data::DataManager *' to 'QMenu *' for 2nd argument
  ```
  (plus the same error for the two updated call sites at lines 184/349). Why expected: the new test passes `DataManager*` as the 2nd ctor arg, which the old signature `( PythonIpcServer*, QMenu*, ActiveViewHost*, QObject* )` cannot accept.
- **GREEN:** `cmake --build build --target test_python_plugin_manager -j"$(nproc)"` → `[100%] Built target test_python_plugin_manager`; then
  ```
  $ ./build/tests/test_python_plugin_manager "[python][isolated][api],[python][isolated][api][headless],[python][isolated][ui],[python][bridge]"
  Filters: [python] [isolated] [api],[python] [isolated] [api] [headless],[python] [isolated] [ui],[python] [bridge]
  ===============================================================================
  All tests passed (49 assertions in 5 test cases)
  ```

## Files changed

- `tests/test_python_plugin_manager.cpp` — 2 ctor call sites updated; new `[python][isolated][api][headless]` TEST_CASE appended (after the `[python][bridge][headless]` case).
- `src/python/isolated/python_app_interface_proxy.h` — `sicnu::data::DataManager` fwd decl; new ctor signature.
- `src/python/isolated/python_app_interface_proxy.cpp` — new includes; ctor passes `dataManager` to bridge; `ui.add_plugin_menu` headless degradation; new `catalog.set_active_layer` IPC branch.
- `src/app/python/python_plugin_adapter.cpp` — (brief deviation, required to compile) passes project-context DataManager into the proxy ctor; added `project_context.h` and `data/data_manager.h` includes.

Commit `64776fde2d` (`4 files changed, 114 insertions(+), 11 deletions(-)`), message verbatim from the brief.

## Self-review findings

- **Completeness:** All brief steps 1–7 executed. Line numbers in the brief were stale by 2 (file had grown since the brief was written); code matched verbatim.
- **Quality/YAGNI:** Implementation is minimal, mirrors the surrounding `if/else if` dispatch style, and reuses existing bridge methods (`setActiveAsset`, `activeAssetId`, `getActiveLayerSummary`, `openPath`). No speculative code added. The `menuTitle` local in `ui.add_plugin_menu` remains read-but-unused (pre-existing pattern, preserved verbatim per the brief).
- **Tests verify real behavior:** The new test drives the actual `handleIpcMessage` dispatch path against a real `DataManager`, asserting observable state through the bridge (active id unchanged on unknown asset, resolves on known asset, source echo, zero registered actions headless). The IPC response payloads themselves are not asserted because the test server is not listening (`sendResponse` is a documented no-op without a socket) — the brief's test asserts the state effects instead, which is the observable behavior.
- **Adapter change risk:** null-safe at both `m_appInterface` and `projectContext()` links; in the existing `[python][adapter][isolated]` test `projectContext` is null so `dataManager` stays null — behavior unchanged (verified: test passes). In GUI mode it now wires the real DataManager, fixing the interim `no_active_layer` regression the task description called out.

## Issues / concerns

1. Brief omission: `python_plugin_adapter.cpp` call site not listed in the brief; fixed + included in the commit. Flagging in case the parent wants it noted in plan docs.
2. The new headless test does not assert the JSON response bodies (`ui_unavailable` / `unknown_asset` / `ok`) since the server has no client — the contract for response shapes is covered only implicitly by the brief. A follow-up could assert responses by hooking a connected socket, but that is outside this task's scope.
3. `menuTitle` remains unused in the `ui.add_plugin_menu` branch (pre-existing; kept verbatim).

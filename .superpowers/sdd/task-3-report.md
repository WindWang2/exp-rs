# Task 3 Report: Inject `DataManager` in `PythonPluginAdapter::initialize`

**Status: DONE**

## What was changed / confirmed already present

Task 2 (commit `64776fde2d`) had already adapted `src/app/python/python_plugin_adapter.cpp` to the brief's Step 1 code (the test target would not compile otherwise). Reconciliation against the brief:

- `#include "project_context.h"` — already present (line 6, after `sicnu_app_interface.h`), matches brief.
- `#include "data/data_manager.h"` — extra include, kept per instructions (reviewer judged it defensible).
- Code block (brief lines 21–34) — already present verbatim: `pluginMenu` from `m_appInterface`, `dataManager` sourced from `m_appInterface->projectContext()->dataManager()` (null when interface or ProjectContext absent), `make_unique<PythonAppInterfaceProxy>( m_workerNode->server, dataManager, pluginMenu )`, then conditional `setActiveViewHost`.
- **Only real change needed:** the explanatory comment. Existing `// Attach UI RPC Proxy Facade` was replaced with the brief's two-line comment:
  ```cpp
  // Attach UI RPC Proxy Facade (headless asset seam: DataManager is the
  // asset authority; menu and view host remain optional GUI enhancements).
  ```
- No other restructuring; file otherwise untouched.

## Test command + results

```
cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python]"
```

- Build: succeeded (only pre-existing `QVariant::Type` deprecation warnings from Qt 6 headers, unrelated to this change).
- Test run:
  ```
  Filters: [python]
  Randomness seeded to: 694656539
  All tests passed (87 assertions in 11 test cases)
  ```
- All 11 `[python]` cases PASS, including `[python][adapter][isolated]` (null-ProjectContext interface → null `DataManager` degradation) and the subprocess cases `[python][isolated]`, `[python][isolated][fault]`.

## Files changed

- `src/app/python/python_plugin_adapter.cpp` — comment only (2 insertions, 1 deletion); the functional code was already in place from Task 2.

## Commit

- `a994c1407c` — `feat(python): inject DataManager asset seam into plugin adapter proxy` (brief's exact message, adapter file only).

Note: working tree had an unrelated pre-existing modification to `.superpowers/sdd/task-2-report.md`; left untouched, not included in the commit.

## Self-review findings / concerns

- Adapter call matches Task 2's proxy ctor: `PythonAppInterfaceProxy( PythonIpcServer*, DataManager*, QMenu*, ActiveViewHost*, QObject* )` at `src/python/isolated/python_app_interface_proxy.h:27` — verified signature, calls `setActiveViewHost` afterwards as designed.
- Null-safety: `m_appInterface->projectContext()` guarded by `&&` before deref; `dataManager` stays `nullptr` in production (no interface) — behavior unchanged, proxy default parameter aligns.
- No concerns.

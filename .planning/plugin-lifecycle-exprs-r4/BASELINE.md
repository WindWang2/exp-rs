# BASELINE — Track 4: Plugin Lifecycle & exprs Registry Rollback (R4)

Measured 2026-09-27, host: CachyOS linux x64, 40 cores / 64 GB RAM.

## 1. Measured baseline

| anchor | value |
|---|---|
| local `master` | `15e5c66b543ef3874cb929f17529ef456bd6c059` |
| `origin/master` | `15e5c66b543ef3874cb929f17529ef456bd6c059` (identical; 0 ahead / 0 behind) |
| worktree | `../exp-rs-plugin-exprs-r4`, branch `hardening/r4-plugin-exprs` off `origin/master` |
| open issues | 0 (`gh issue list` → empty) |

The writing-time SHA `15e5c66b5` (PR #1333 merge point) is still current — no drift.

## 2. Open PRs and file-overlap map

Measured via `gh pr list --state open` (5 open):

| PR | branch | touches (relevant to this track) | overlap policy |
|---|---|---|---|
| #1334 | `fix/review-p1-security` | `src/core/plugin_host.{h,cpp}` (legacy PluginHost whitelist, IID pre-check, dir checks), `tests/CMakeLists.txt` (+7, new target `test_plugin_host_allowlist`), `tests/test_plugin_host_allowlist.cpp` (new) | **src/core is OUTSIDE this track's whitelist** — P1-9 is explicitly left open by #1334 and claimed by this track. tests/CMakeLists.txt: append-only registrations at file end; rebase after #1334 lands. |
| #1335 | `fix/review-p0-build-restore` | `tests/CMakeLists.txt` (+65, ctest discovery), `cmake/SicnuCatchAddTests.cmake` (new) | tests/CMakeLists.txt overlap: append-only + rebase policy. |
| #1336 | `hardening/closure-ui-runtime-r4` | i18n/help/lab-pack; **no plugin/exprs files** | none. |
| #1337 | `hardening/closure-workflow-contracts-r4` | workflow/agent/contracts; no plugin/exprs files | none. |
| #1338 | `hardening/closure-io-processing-r4` | io/processing; no plugin/exprs files | none. |

Note: #1334's legacy-PluginHost whitelist work lives in **`src/core/plugin_host.cpp`**, not `src/plugins/` as the prompt assumed. `src/core` is outside this track's file whitelist, so WP-D boundary tests pin behavior of the surfaces that exist on master **inside the whitelist**: `src/sdk/exprs/path_policy.*`, manifest validation/containment, registry trust gates. #1334's whitelist itself (world-writable dirs, symlink escape, IID pre-check) is *not yet on master* — those gates cannot be pinned red/green from this branch without touching out-of-scope files; see DECISIONS.md (D-2).

## 3. P1-9 three failing TEST_CASEs (title-anchored, measured on master)

`tests/test_exprs_plugin_loader.cpp`: **2197 lines / 97,744 bytes / 34 TEST_CASEs**
(prompt's writing-time numbers 2032 lines / 99,941 B / 37 TCs have drifted; measured wins).

| title | measured line | tag |
|---|---|---|
| "hot reload swaps in a valid new manifest and refuses a broken one" | `:493` | `[plugin][reload][p12]` |
| "installOrUpgrade installs a fresh package, then atomically upgrades it" | `:1138` | `[plugin][upgrade][p13]` |
| "installOrUpgrade rolls back when the new version cannot load" | `:1226` | `[plugin][upgrade][p13]` |

All three titles match the prompt's anchors exactly. Evidence of the historical failure:
PR #1334 description, verification table: "`test_exprs_plugin_loader`：31/34，失败的 3 个是既有问题（P1-9）"
(31/34 passing; the 3 failures are the pre-existing P1-9). Note #1334 measured on **Windows**;
Linux reproduction is Phase 0's red-run job (WP-A起点).

## 4. Existing rollback-family contracts (independence references)

Measured TEST_CASE anchors in `tests/test_exprs_plugin_loader.cpp`:

- `:570` "hot reload rolls back to the snapshot when the new code cannot load"
- `:638` "hot reload aborts on a failed state migration with the old version loaded"
- `:1175` "installOrUpgrade refuses a bad manifest before touching the install"
- `:1199` "installOrUpgrade aborts on a failed state migration with v1 untouched"
- `:1263` "installOrUpgrade refuses the drain when the plugin is busy"
- `:1730` "installOrUpgrade refuses a policy-gated manifest before draining v1"
- `:1783` "concurrent installOrUpgrade on the same id refuses the second"
- `:1840` "installOrUpgrade refuses a plugin shadowed by an earlier root"
- `:1885` "installOrUpgrade rolls back when the package fails checksum verification"
- `:1928` (region) "registry teardown joins an in-flight snapshot capture"

## 5. Directory / file counts (measured)

| anchor | value |
|---|---|
| `src/plugins` .h/.cpp | 31 |
| `src/sdk` .h/.cpp | 51 |
| `src/plugins` + `src/sdk` combined | 82 |
| `src/sdk/exprs/plugin_registry.cpp` | 100,203 bytes (3,0xx lines) |
| plugin/exprs test files in `tests/` | 18 (list below) |

Test files: `test_exprs_external_process.cpp`, `test_exprs_external_process_win.cpp`,
`test_exprs_ipc.cpp`, `test_exprs_plugin_loader.cpp`, `test_exprs_plugin_system.cpp`,
`test_exprs_workflow_schema.cpp`, `test_plugin_capabilities.cpp`,
`test_plugin_execution_barrier.cpp`, `test_plugin_host.cpp`, `test_plugin_host_process.cpp`,
`test_plugin_manifest.cpp`, `test_plugin_model_bridge.cpp`, `test_plugins_runtime_host.cpp`,
`test_plugin_ui_placement.cpp`, `test_plugin_ui_schema.cpp`, `test_plugin_ui_schema_host.cpp`,
`test_python_plugin_host.cpp`, `test_python_plugin_manager.cpp`.

## 6. Toolchain & build recipe (this host)

| tool | path |
|---|---|
| cmake 4.1.2 | `/home/kevin/tools/cmake-4.1.2-linux-x86_64/bin/cmake` |
| ninja | `/home/kevin/tools/ninja-bin/ninja` |
| gcc | 16.2.1 (`/usr/sbin/g++`) |
| Qt6 | 6.11.2 (system, `/usr/lib/cmake/Qt6`) |
| GDAL 3.13.3 / PROJ 9.8.1 / GEOS 3.15.0 | local prefix `/home/kevin/pwb-sdks/root/usr` (recipe inherited from `exp-rs-hardening-planner-repair-capability-graph/build/CMakeCache.txt`, 2026-09-23) |

Configure: `cmake --preset dev-default -G Ninja -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`
(Debug, `ENABLE_TESTS=ON`; fresh `build-dev/` inside the worktree).
Build cap `-j2`; test `ctest -j1` with `QT_QPA_PLATFORM=offscreen`.

## 7. Master red-run (WP-A starting evidence)

Filled in after the fresh build completes — see EVIDENCE.md §1 for the captured log.

# R28-T04 Baseline

- **BASE_SHA**: `50cc3255c5eb30cd247d7c065026607f36f90f03` (= origin/master @ fetch 2026-11-04T21:03:52Z, no drift)
- **Worktree**: `../exp-rs-r28-t04-workbench-cartography-20261104T210902Z`, branch `agent/r28-t04-workbench-cartography-20261104T210902Z`
- **Main repo state (read-only)**: checked out on `fix/ui-review-2026-11-05` @ `4b27e1c089` **with uncommitted modifications** to app/test files — active parallel work on PR #1813. Not touched.
- **Host**: 16-core / 62 GB, load ~3.4 at start, 681 GB free on /home. ccache 45% hit rate. Other R28 worktrees (t01/t02/t03) exist; host-build lock serializes heavy steps.

## Incoming facts (from PR/issue recon, to re-verify on this tree)

1. **#1811** (merged 2026-11-03, in BASE_SHA) fixed: #1745/#1746/#1747 (P1), #1788–#1795 (P2), #1805 F6/F13/F17, engaged private-network policy at GUI entry. Declared NOT done: **#1805 F7/F12/F14/F18**.
2. **#1812** (merged 2026-11-04, in BASE_SHA) fixed 131 swarm findings incl. `confirmMapOutput` re-feeding repaired mapspec, `atlas sort_expression` failures silently discarding the sort. Its merge-time status admits remaining failures: **cartography atlas deliveries (pre-existing on master)**, TaskCenter/ExecutionPlane cancellation cluster, snapshot drift, 77/9776 fast red.
3. **#1561** (spectral profile sync network read): FIXED_IN_MASTER per #1811 (commit `923595d0e2`). Must spot-verify on this tree (WP02 acceptance).
4. **PR #1813 (OPEN)** — `fix/ui-review-2026-11-05`: UI polish (shared tab group, palettes, uiScale, copilot opt-in, pagination, pipeline editor undo/redo, QSS/i18n). Its edits to `cartography_dock.cpp`/`experiment_studio_dock.cpp` are layout-only; **does not touch F14 lifetime / F18 destructor-join logic**. Overlap risk: same files, different hunks; re-check before merge.
5. **#1805 sub-items still open by #1811's own declaration**:
   - F7 `orthorectification_dialog.cpp:31,155` GDALOpen on GUI thread
   - F12 `qgslayoutdesignerdialog.cpp:1228-1265` template-load failure wipes layout, no undo
   - F14 `cartography_dock.cpp:315-355` lifetime leak when no terminal state arrives
   - F18 `experiment_studio_dock.cpp:478-492` destructor joins worker, up to 60 s GUI block
6. **Ownership**: write = `src/app/**` (excl. `agent_ops/`), `src/ui/**`, `resources/styles*.qss`, `src/agent/cartography|mapspec|layout_tools|symbology/**`, `src/app/workflow/**`, `src/app/shell/workflow_session_controller.*`; read-only reliance on T02's `src/geospatial/**` ImportPreflight/ImportSession, T05's workflow backend. `src/teaching_admin/` + workflow backend = T05; approval/thread adapters = T03.

## Build recipe (mirrors r27-t04)

```
cmake -S . -B build-r28-t04 -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER_LAUNCHER="<WT>/cmake/raise-compiler-stack.sh;ccache" \
  -DCMAKE_CXX_COMPILER_LAUNCHER="<WT>/cmake/raise-compiler-stack.sh;ccache" \
  -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON
```
Heavy steps under `resource_guard.py --lock-dir ~/.cache/exp-rs-agent/host-build` → worktree build lock; `-j2`; `QT_QPA_PLATFORM=offscreen ctest -j1`.

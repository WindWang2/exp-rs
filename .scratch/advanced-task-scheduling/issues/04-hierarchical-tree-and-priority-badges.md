# 04 — Hierarchical Tree & Priority Badges in TaskCenterDock

**What to build:** Update `TaskCenterDock` UI to render child tasks indented under parent items and format Priority badges (`[高]`, `[中]`, `[低]`).

**Blocked by:** 03 — Upstream Output Parameter Substitution (${task.<parent_id>.output})

**Status:** ready-for-agent

- [ ] Add Priority column to `m_taskTree` in `src/app/panels/task_center_dock.{h,cpp}`
- [ ] Format Priority badges (`[高]`, `[中]`, `[低]`) with color styling
- [ ] Render child tasks as child `QTreeWidgetItem` nodes under parent task nodes
- [ ] Add GUI unit tests in `tests/test_task_center_dock.cpp`

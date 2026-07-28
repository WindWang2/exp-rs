# 04 — TaskCenterDock UI Visualization for DAG Pipelines & Priorities

**Type:** wayfinder:prototype

## Question

How should parent-child task relationships (tree hierarchy indentation) and task priority badges (High/Normal/Low) be rendered in `TaskCenterDock`?

## Blocked by

03 — Dynamic Upstream Output Data Ingestion

**Status:** closed

### Resolution
- Rendered child tasks as indented tree nodes under parent tasks in `m_taskTree`.
- Added "Priority" column with color-coded badges (`[高]`, `[中]`, `[低]`).
- Documented in [0005-task-center-dock-dag-and-priority-visualization.md](../../docs/adr/0005-task-center-dock-dag-and-priority-visualization.md).

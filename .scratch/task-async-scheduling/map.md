# Wayfinder Map: Advanced Task Async Scheduling & DAG Pipelines

## Destination

Implement Advanced Task Async Scheduling for SICNU GEO RS, featuring Directed Acyclic Graph (DAG) Task Pipelines, parent-child dependency execution, Priority Queue ordering (High/Normal/Low), CPU/memory resource throttling, and Task Center UI visualization.

## Notes

- Domain Glossary: `CONTEXT.md` (Algorithm Engine, Task Center, Algorithm Task, Task Pipeline, Resource Throttler)
- ADRs: `docs/adr/0001-algorithm-engine-and-task-center.md`
- Primary C++ Modules: `src/processing/framework/task_center.{h,cpp}`, `src/processing/framework/resource_throttler.{h,cpp}`, `src/app/panels/task_center_dock.{h,cpp}`

## Decisions so far

- [Destination & Scope](docs/adr/0001-algorithm-engine-and-task-center.md) — Unified backend `AlgorithmEngine` + `TaskCenterDock` with DAG Pipelines and Priority Resource Throttling.
- [01 — Priority Queueing & Resource Throttling Strategy](issues/01-priority-queueing-and-resource-throttling.md) — Priority enum (High > Normal > Low) and auto thread cap `hardware_concurrency() - 1`.
- [02 — Task Pipeline DAG Representation & Dependency Gating](issues/02-task-pipeline-dag-representation.md) — Parent ID list gating with cascade completion unblocking and failure cancellation.
- [03 — Dynamic Upstream Output Data Ingestion](issues/03-upstream-output-data-ingestion.md) — Template placeholder substitution `${task.<parent_id>.output}` into downstream parameter maps.
- [04 — TaskCenterDock UI Visualization for DAG Pipelines & Priorities](issues/04-task-center-dock-ui-visualization.md) — Hierarchical tree node indentation for child tasks and color-coded Priority badges.

## Not yet specified

- Distributed/Remote execution nodes across RPC server clusters (out of scope for local desktop engine).

## Out of scope

- Real-time cloud cluster GPU task virtualization.

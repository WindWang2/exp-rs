# 03 — Upstream Output Parameter Substitution (${task.<parent_id>.output})

**What to build:** Implement template placeholder substitution in parameter maps so downstream child tasks automatically consume upstream parent `outputLayerPath` values.

**Blocked by:** 02 — DAG Parent Dependency Gating & Cascade Cancellation

**Status:** ready-for-agent

- [ ] Implement `${task.<parent_id>.output}` placeholder parser in `TaskCenter`
- [ ] Substitute output path parameters prior to launching child tasks
- [ ] Add unit tests in `tests/test_task_center.cpp`

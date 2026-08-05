# 02 — WorkflowRuntime Integration & Unit Tests

**What to build:** Wire `WorkflowRuntime` to attach `pipelineId` when submitting pipelines to `TaskCenter`. Update `test_workflow_runtime.cpp` to verify authoritative step state resolution.

**Blocked by:** 01 — Pipeline ID Binding & Authoritative TaskCenter Status Query

**Status:** ready-for-agent

- [ ] `WorkflowRuntime` binds `pipelineId` to `WorkflowSession`
- [ ] `test_workflow_runtime.cpp` verifies step completion querying via `TaskCenter`
- [ ] 100% test suite passage across `test_workflow_runtime` and repository

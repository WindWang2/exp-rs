# 01 — Pipeline ID Binding & Authoritative TaskCenter Status Query

**What to build:** Add `long pipelineId = -1` to `WorkflowSession` and `SessionSnapshot`. Query `TaskCenter::instance().getPipelineInfo(pipelineId)` when bound to resolve authoritative step execution states.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] `WorkflowSession` supports `setPipelineId(long pipelineId)` and `pipelineId()`
- [ ] `SessionSnapshot` includes `long pipelineId = -1`
- [ ] `WorkflowSession::snapshot()` queries `TaskCenter::getPipelineInfo` when `pipelineId >= 0`
- [ ] Existing `WorkflowSession` unit tests pass cleanly

# 04 — Wave D: Pipeline Canvas Synchronization & Interactive DAG Plan Approval

**What to build:**
Multi-step DAG plans returned by LLM automatically instantiate as visual `WorkflowDefinition` node graphs on `PipelineCanvasWidget` for interactive preview, parameter tuning, and 1-click plan approval/execution via `AgentWorkflowExecutor::executeAgentPlan`.

**Blocked by:** 03 — Wave C: AgentCopilotDockWidget Right Panel & Streaming Chat History UI

**Status:** ready-for-agent

## Acceptance Criteria
- [ ] Parse multi-step agent plans from LLM responses into `WorkflowDefinition` objects.
- [ ] Automatically load generated workflows onto `PipelineCanvasWidget` with automatic spatial coordinate layout $(X,Y)$.
- [ ] Display interactive Plan Approval cards in `AgentCopilotDockWidget` with "▶ Run Pipeline" and "👁️ View in Canvas" buttons.
- [ ] Connect "Run Pipeline" button to `AgentWorkflowExecutor::executeAgentPlan()`, reporting real-time node status overlays (⚪ Idle, 🔵 Running, 🟢 Success, 🔴 Failure) on both chat stream cards and node graph canvas.
- [ ] Add integration Catch2 tests in `tests/test_agent_canvas_sync.cpp` verifying plan parsing, node graph instantiation, and plan execution.

# Findings — TaskCenter Deepening & Native DAG Task Pipeline Engine

## Research & Architectural Discoveries

### 1. Codebase Architecture Friction
- **Shallow UI Queue Controller**: Prior to deepening, `WorkflowSessionController` (`src/app/shell/`) maintained a manual step queue (`m_executionQueue`, `m_currentQueueIndex`) inside Qt UI slots. Upon task completion, `onTaskUpdated` stepped manually to the next index and re-submitted jobs via `submitJob()`.
- **Fragmented DAG Execution**: DAG execution logic was split between `WorkflowSessionController` (UI stepping), `WorkflowRuntime` (session map pass-throughs), and `TaskCenter` (single task execution), preventing headless CLI and LLM Agent execution from sharing identical pipeline semantics.

### 2. Deepening Opportunities Surfaced
- **HTML Review Report**: Generated self-contained HTML report (`file:///tmp/architecture-review-20260729.html`) with Tailwind and Mermaid diagrams, analyzing 4 candidates:
  1. Collapse `McpServer` worker threads into `AtomicAlgorithmRegistry` (`Strong`)
  2. Deepen `TaskCenter` to absorb `WorkflowSessionController` DAG queue (`Strong`) — **Selected by User**
  3. Unify Python App Interface proxies behind `AppInterfaceBridge` (`Worth exploring`)
  4. Deepen `WorkspaceSnapshot` to absorb `AgentContextResolver` formatting (`Worth exploring`)

### 3. Key Design Choices (ADR 0016)
- **Single Seam**: `TaskCenter::submitPipeline(WorkflowDefinition)` and `submitPipelineJson(std::string)` act as the single entry point for DAG execution.
- **Dynamic Parameter Substitution**: Downstream task parameters containing `$stepId.output` or `${stepId.output}` are resolved dynamically in `TaskCenter::processNextQueuedTasks()` when parent tasks finish.
- **Reactive UI Badging**: `WorkflowSessionController` listens to `TaskCenter::taskUpdated(info)` to emit `stepStatusChanged(stepId, statusStr)` for real-time node badging on `PipelineCanvasWidget`.

### 4. Modified Files Reference
- `CONTEXT.md`: Recorded ADR 0016
- `src/processing/framework/task_center.h` & `task_center.cpp`: Added `submitPipeline`, `submitPipelineJson`, `PipelineExecutionInfo`, enhanced placeholder substitution
- `src/app/shell/workflow_session_controller.h` & `workflow_session_controller.cpp`: Refactored to delegate DAG submission to `TaskCenter` and reactively observe task updates
- `src/operators/rs/rs_operators_init.cpp`: Fixed includes and namespace qualification
- `tests/test_task_center.cpp`: Added headless DAG pipeline test case

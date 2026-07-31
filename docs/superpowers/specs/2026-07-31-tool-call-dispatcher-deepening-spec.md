# Tool Call Dispatcher Deepening Specification

**Status:** Ready for Implementation  
**Date:** 2026-07-31  
**Subsystem:** `src/processing/framework/`, `src/agent/`  
**ADR Ref:** [ADR 0012: Atomic Algorithm Adapter & LLM Agent Tool Calling](file:///home/kevin/projects/exp-rs/CONTEXT.md#L119-L125)

---

## Problem Statement

Currently, callers of `ToolCallDispatcher` (such as `AgentCopilotDockWidget`, `MCPServer`, and `AgentWorkflowExecutor`) must manually manage `TaskCenter` task completion polling, status monitoring, timeout formatting, and output dataset publication through `OutputCommitter`. As a result, `ToolCallDispatcher` is shallow, delegating tool parsing to `TaskCenter` while leaving completion payload assembly and error handling fragmented across callers.

---

## Solution

Deepen `ToolCallDispatcher` into the single execution and response seam for LLM tool calls. `ToolCallDispatcher` encapsulates `TaskCenter` task completion watching, transactional output asset publication via `OutputCommitter`, and synchronous/asynchronous tool dispatch via a unified `dispatchAndAwait` interface, returning standardized Tool Result JSON payloads (`status`, `output`, `errorMessage`, execution metrics) directly to callers.

---

## User Stories

1. As an AI Copilot user, I want single tool call results and status errors to be formatted consistently, so that tool call execution outputs can be reliably consumed by the LLM system prompt.
2. As an AI Agent developer, I want `ToolCallDispatcher` to handle `TaskCenter` completion watching automatically, so that UI and agent dock widgets do not duplicate task polling or status listening logic.
3. As a Remote Sensing analyst, I want tool-generated raster outputs to be transactionally registered in `DataManager` via `OutputCommitter`, so that output paths reported by tool calls are immediately stable and registered.
4. As a Headless CLI / MCP server developer, I want a synchronous `dispatchAndAwait` entry point, so that I can submit a tool call envelope and await a fully formatted result JSON payload without spinning custom event loops.
5. As a test engineer, I want `ToolCallDispatcher` to accept dependency-injected submission sinks and completion watchers, so that unit tests can verify tool dispatching without requiring a full GUI main window.

---

## Implementation Decisions

- Module `ToolCallDispatcher` in `src/processing/framework` will be deepened to serve as the single seam for envelope parsing, task submission, completion watching, output asset committing, and synchronous/asynchronous execution.
- Interface additions:
  - Add `setOutputCommitter(OutputCommitter *committer)` to allow optional injection of `OutputCommitter`.
  - Provide a default constructor/factory initializer that binds `SubmissionSink` to `TaskCenter::enqueueTask` and `CompletionWatcher` to `TaskCenter` task status updates.
  - Introduce `dispatchAndAwait(const Json::Value &envelope, std::chrono::milliseconds timeout)` as the primary synchronous dispatch entry point.
- Completion watching & result formatting:
  - Upon task completion, `ToolCallDispatcher` formats a standardized Tool Result JSON payload (`status`, `errorMessage`, `output`, execution metrics).
  - If `OutputCommitter` is configured and the task produced a file path, `ToolCallDispatcher` transactionally publishes the asset and replaces `payload["output"]` with the committed stable path (or populates `payload["commitError"]` on failure).
- Synchronous / Asynchronous symmetry:
  - `submitBlocking()` delegates directly to `dispatchAndAwait()`, ensuring identical error handling, timeout formatting, and `OutputCommitter` processing across all synchronous callers.

---

## Testing Decisions

- Tests will target external module behavior at the highest seam (`ToolCallDispatcher` API surface), verifying input envelopes against output result JSON payloads without asserting on internal polling loops.
- Target test module: `tests/test_tool_call_dispatcher.cpp`.
- Prior art: `tests/test_tool_call_dispatcher.cpp`, `tests/test_atomic_algorithm_registry.cpp`, `tests/test_ui_task_center_contract.cpp`.
- Verification cases:
  - Dispatching valid tool envelopes to fake submission sinks and verifying expected result JSON formatting.
  - Default `TaskCenter` completion watching when executing asynchronous tasks.
  - `OutputCommitter` integration: confirming output path rewriting to committed stable path upon completion.
  - Synchronous `dispatchAndAwait` timeout handling and error payload generation.

---

## Out of Scope

- Multi-step DAG workflow plan decomposition (`AgentWorkflowExecutor` plan approval pipeline).
- UI layer tree widget rendering or canvas display toggles.
- Modifying `AtomicAlgorithmRegistry` parameter descriptor validation schemas.

---

## Further Notes

- Aligns with ADR 0012 (`AtomicAlgorithmAdapter`), ADR 0021 (`OutputCommitter`), and updated [`CONTEXT.md`](file:///home/kevin/projects/exp-rs/CONTEXT.md#L39-L42).

# Unified Task Center Dispatch Specification

**Status:** Ready for Implementation  
**Date:** 2026-07-28  
**Subsystem:** `src/processing/framework/`, `src/agent/`  
**ADR Ref:** [ADR 0012: Atomic Algorithm Adapter & LLM Agent Tool Calling](file:///home/kevin/projects/exp-rs/CONTEXT.md#L119-L125)

---

## 1. Problem Statement

Currently, single-step LLM tool calls (e.g. `rs:spectral_index` triggered via `LlmStreamingClient` or `AtomicAlgorithmRegistry::executeToolCall`) execute synchronously on the caller thread, bypassing `TaskCenter`. As a result, single tool calls lack progress reporting, priority resource scheduling, background cancelability, and visibility in `TaskCenterDock`.

---

## 2. Solution

Route both single-step LLM tool calls and multi-step `WorkflowDefinition` DAG plans through `TaskCenter`. When `AtomicAlgorithmRegistry::executeToolCall` is invoked with an asynchronous flag or from `LlmStreamingClient`, it enqueues an `AlgorithmTask` in `TaskCenter`, returning a tracked `taskId` and executing via `ResourceThrottler` background worker threads.

---

## 3. Implementation Details

### 3.1 `TaskCenter::enqueueToolCall` Method
- Add `long enqueueToolCall( const std::string &jsonToolCall, bool autoLoad = true );` to `TaskCenter`.
- Parses JSON tool call payload, extracts algorithm ID and parameters, creates an `AlgorithmTaskInfo` with `TaskStatus::Queued`, and submits to the background worker pool.
- Emits `taskAdded` signal, causing `TaskCenterDock` UI to update automatically.

### 3.2 `AtomicAlgorithmRegistry` Integration
- Add `long submitToolCall( const std::string &jsonToolCall );` to `AtomicAlgorithmRegistry`.
- Delegates task creation directly to `TaskCenter::instance().enqueueToolCall(jsonToolCall)`.

### 3.3 `LlmStreamingClient` Async Tool Execution
- When `LlmStreamingClient` receives a tool call chunk, it calls `TaskCenter::instance().enqueueToolCall(toolCallJsonStr)`.
- Listens to `TaskCenter` signals (`taskUpdated`, `taskLogAdded`) to report real-time tool execution progress back to the AI Agent chat stream.

---

## 4. Testing Decisions

- **Testing Seam**: Catch2 unit tests in `tests/test_atomic_algorithm_registry.cpp` and `tests/test_llm_streaming_client.cpp`.
- **Validation**: Verify that `enqueueToolCall()` returns a valid `taskId`, enqueues the task in `TaskCenter`, and emits `taskAdded` and `taskUpdated` signals upon completion.

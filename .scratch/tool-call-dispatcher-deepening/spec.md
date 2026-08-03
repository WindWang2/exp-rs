# Specification: Deepening ToolCallDispatcher into a Direct Execution Gateway

## Problem Statement

Currently, `ToolCallDispatcher` requires callers to inject custom `SubmissionSink` and `CompletionWatcher` lambda functions into its constructor to dispatch algorithms and observe completion. In production callers like `AgentCopilotDockWidget` and `McpServer`, this creates repetitive, shallow lambda glue code that manually bridges `TaskCenter::enqueueTask` and `TaskCenter::waitForTask`. This adds architectural friction, decreases locality, and forces callers to manage execution callback boilerplate.

## Solution

Deepen `ToolCallDispatcher` into a direct execution gateway that natively delegates algorithm submission and completion watching to `TaskCenter::instance()` and `AtomicAlgorithmRegistry::instance()` by default. Callers can instantiate `ToolCallDispatcher` without injecting custom submission sinks or completion watchers, while test suites retain the ability to inject custom fake sinks for unit testing.

## User Stories

1. As an LLM Agent, I want to submit a single tool call envelope directly to `ToolCallDispatcher`, so that the algorithm is validated and executed asynchronously without requiring caller-side callback plumbing.
2. As a Desktop User using the AI Copilot Dock, I want tool calls issued by the copilot to seamlessly dispatch to `TaskCenter` and stream completion payloads back to the chat UI.
3. As a Headless MCP Client, I want tool call execution requests to run directly through `ToolCallDispatcher` and return transactional committed asset paths without extra wrapper code.
4. As a Developer, I want `ToolCallDispatcher()` to provide zero-boilerplate default execution while still supporting optional custom sinks in unit tests.
5. As a System Architect, I want all envelope parsing, id normalization, parameter conversion, and execution dispatching to have a single authoritative owner in `ToolCallDispatcher`.

## Implementation Decisions

- **Default Execution Seam**: Modify `ToolCallDispatcher` constructor to make `SubmissionSink` and `CompletionWatcher` optional parameters. When omitted or null, `ToolCallDispatcher` defaults to:
  - `SubmissionSink`: `TaskCenter::instance().enqueueTask(algorithmId, params)`
  - `CompletionWatcher`: Asynchronous task completion watching via `TaskCenter::instance().taskUpdated` signal or background thread `waitForTask`.
- **Parameter Handoff**: Use `sicnu::processing::jsonParamsToVariantMap` and `variantToJsonValue` for all JSON/QVariantMap conversions.
- **Envelope Classification**: Preserve `classify()` and `rejectionReason()` contracts for `PlanRequest` and `ToolCall` detection.
- **Transactional Asset Output Committing**: Retain `OutputCommitterHandler` and `setDataManager()` integration for transactional output committing.

## Testing Decisions

- **Seam**: Test `ToolCallDispatcher` through its public API (`submit`, `dispatchAndAwait`, `classify`, `rejectionReason`).
- **Tests to build/update**:
  - `test_tool_call_dispatcher.cpp`: Update tests to cover zero-arg default `ToolCallDispatcher` construction and direct `TaskCenter` dispatch.
  - Verify rejection handling for malformed envelopes and missing required parameters.
  - Verify end-to-end task dispatching and payload formatting.

## Out of Scope

- Modifying OpenAI tool definition schemas or prompt engineering templates.
- Changing `TaskCenter` internal concurrency scheduling algorithms.

## Further Notes

Aligned with ADR 0021 (Tool Call Dispatching) and ADR 0053 (Deep Module Consolidation).

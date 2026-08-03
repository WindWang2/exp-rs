# Specification: Unifying Workflow Pipeline State Models

## Problem Statement

Currently, `WorkflowSession` tracks step completion and session state independently from `TaskCenter::PipelineExecutionInfo`. When executing multi-step pipelines via `TaskCenter::submitPipeline`, dual state models exist in parallel, forcing callers to sync states manually and causing potential state drift during step retries or cancellations.

## Solution

Unify workflow session state by making `TaskCenter::PipelineExecutionInfo` the authoritative state model for pipeline execution DAGs. Refactor `WorkflowSession` to query `TaskCenter` pipeline status when linked to an active `pipelineId`, while maintaining pure parameter resolution for wizard steps.

## User Stories

1. As a Developer, I want `WorkflowSession` to read step status directly from `TaskCenter::PipelineExecutionInfo` when a pipeline is running in `TaskCenter`, eliminating state drift.
2. As a GUI User, I want workflow wizard steps to display real-time pipeline step progress and terminal state without manual sync polling.
3. As a Headless Runner, I want pipeline cancellations in `TaskCenter` to instantly reflect as canceled step states in `WorkflowSession`.

## Implementation Decisions

- **Pipeline ID Binding**: Add `long pipelineId = -1` to `WorkflowSession` and `SessionSnapshot`.
- **Authoritative Status Query**: When `pipelineId >= 0`, `WorkflowSession::markStepComplete` and `snapshot()` query `TaskCenter::instance().getPipelineInfo(pipelineId)` to resolve step statuses.
- **Backwards Compatibility**: Standalone wizard sessions without an active `TaskCenter` pipeline preserve local `m_completed` step tracking.

## Testing Decisions

- **Seam**: Test `WorkflowSession` and `WorkflowRuntime` with Catch2 unit tests.
- **Tests to build/update**:
  - `test_workflow_runtime.cpp`: Verify pipeline binding and state resolution with `TaskCenter`.

## Out of Scope

- Changing `WorkflowDefinition` JSON schema structure.

## Further Notes

Aligned with ADR 0028 and ADR 0053 (Deep Module Consolidation).

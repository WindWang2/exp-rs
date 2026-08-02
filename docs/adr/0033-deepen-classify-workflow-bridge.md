# ADR 0033: Deepen RsClassifyWorkflowBridge Signal Synchronization

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`RsClassifyWorkflowBridge` was a thin helper class requiring `QgsClassificationMainWindow` to manually invoke `gotoStep`, `syncCompletionsFromController`, `setSourceRasterArtifact`, and `setClassifiedOutputArtifact` across multiple UI slots to mirror classification state into `WorkflowRuntime`.

## Decision

1. **`QObject` Bridge Deepening**: `RsClassifyWorkflowBridge` inherits from `QObject` and exposes `bindController(RsClassifyWorkflowController *controller)`.
2. **Automated Signal Binding**: Upon `bindController`, the bridge automatically opens the `"lab.classify.supervised"` workflow session in `WorkflowRuntime` if not already open, and connects directly to `controller->currentStepChanged` and `controller->completionChanged` signals.
3. **Caller Simplification**: `QgsClassificationMainWindow` delegates step transition and completion synchronization to the bridge by calling `m_workflowBridge->bindController(m_workflow.get())`.

## Consequences

- **Locality**: Classification workflow session mapping and signal observation concentrate inside `RsClassifyWorkflowBridge`.
- **Leverage**: Main window GUI code is relieved of manual step synchronization boilerplate.
- **Testability**: `RsClassifyWorkflowBridge` signal synchronization can be verified headlessly.

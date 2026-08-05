# ADR 0038: Consolidate RsClassifyWorkflowBridge Artifact Sync

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`RsClassifyWorkflowBridge` required callers in `QgsClassificationMainWindow` to manually invoke `setSourceRasterArtifact` and `setClassifiedOutputArtifact` whenever raster paths were set on `RsClassifyWorkflowController`.

## Decision

1. **Automated Artifact Sync**: Deepen `RsClassifyWorkflowBridge::syncCompletionsFromController` to automatically extract `inputRasterPath()` as `"source_raster"` and `outputRasterPath()` as `"classified_output"` from `RsClassifyWorkflowController`.
2. **Backward-Compatible Setters**: Retain explicit setters (`setSourceRasterArtifact` and `setClassifiedOutputArtifact`) for manual override access.
3. **Reduced UI Boilerplate**: Eliminate manual artifact setter invocations across classification UI event handlers.

## Consequences

- **Locality**: Artifact synchronization concentrates inside `RsClassifyWorkflowBridge` signal handling.
- **Maintainability**: Classification session state stays synchronized with controller state automatically.

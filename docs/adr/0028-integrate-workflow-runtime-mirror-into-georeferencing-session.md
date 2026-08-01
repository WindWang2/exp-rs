# ADR 0028: Integrate Workflow Runtime Mirror into Georeferencing Session

- **Status**: Approved
- **Date**: 2026-08-02
- **Deciders**: AI Assistant, Project Lead

## Context and Problem Statement

`RsGeorefWorkflowBridge` was a sidecar adapter responsible for opening a `sicnu::workflow::WorkflowRuntime` session (`lab.georef.image_to_map`) and mirroring `source_raster`, `gcp_count`, and `output` artifacts.

Both `QgsGeorefShellWindow` and `QgsGeorefImageToMapWindow` were forced to perform dual writes: calling `RsGeoreferencingSession` for actual GCP/raster state while manually notifying `mWorkflowBridge` to sync artifact strings and advance steps.

This created unnecessary coupling and dual-write boilerplate across the UI shell layer.

## Decision Drivers

1. **Eliminate Dual-Write Boilerplate**: `RsGeoreferencingSession` already mutates GCP lists, source raster paths, and warp task status. It should mirror workflow runtime artifacts automatically.
2. **Deepen Georeferencing Session**: Absorb workflow runtime session lifecycle into `RsGeoreferencingSession` so the UI layer interacts with a single, deep session module.
3. **Automatic Step Progression**: Automatically mark steps complete (`open_image`, `gcp`, `warp`) when corresponding state changes occur in the session.

## Considered Options

1. **Keep `RsGeorefWorkflowBridge` Sidecar**: Continue manually calling `mWorkflowBridge` methods from UI shell windows.
2. **Integrate Workflow Runtime Mirror into `RsGeoreferencingSession`**: Move `WorkflowRegistry` and `WorkflowRuntime` ownership into `RsGeoreferencingSession`, enabling automatic artifact and step syncing.

## Decision Outcome

Chosen Option: **Option 2**.

### Consequences

- **Positive**:
  - Eliminates `RsGeorefWorkflowBridge` sidecar class (`rs_georef_workflow_bridge.h`/`.cpp`).
  - `RsGeoreferencingSession` automatically updates `gcp_count` and `source_raster` artifacts when GCPs or source raster paths change.
  - UI shell windows only need to call `mGeorefSession.enableWorkflowMirror()`.
  - Simplifies testing and guarantees state-to-workflow consistency.
- **Negative**:
  - `RsGeoreferencingSession` links directly against `sicnu_workflow` library (already linked in `qgis_app_georef`).

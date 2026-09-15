# CURRENT_ARCHITECTURE — D18 snapshot

## Shell

- `QgisDesktopWindow` mounts docks/panels; `ProjectContext` owns Data/Display/Workspace.
- `SelectionContext` + `WorkbenchObjectRef` = selection identity (Workbench 10).
- **New:** `MissionContext` = serializable mission aggregate of typed refs + spatial/temporal/map/workflow handles.

## Workflow

See `AUDIT_WORKFLOW.md`. Designer IR 2.0 vs Engine 2.0 vs Agent IR 1.0.

## Studios

Specialist UIs remain. D18 binds mission refs (ClassificationStudioWidget mission input/result; TemporalWorkbenchPanel::exportTemporalContext).

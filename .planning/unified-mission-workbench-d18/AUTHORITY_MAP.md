# AUTHORITY_MAP — D18

| Domain | Authority | MissionContext role |
|--------|-----------|---------------------|
| Project file / layers | QgsProject | `projectRef` (path or project id string) |
| Data assets | DataManager / AssetId | `assets[]` as WorkbenchObjectRef(Asset) |
| Governance results | WorkspaceService | `results[]` |
| Datasets | DatasetStore | `datasets[]` |
| Experiments | ExperimentStore | `experiments[]` |
| Models | ModelCatalog | `models[]` |
| Map layers | QgsMapLayer::id | `layers[]` (ids only) |
| Workflow document (designer) | IR 2.0 | `activeWorkflowId` + fingerprint + schemaVersion |
| Workflow run (production) | WorkflowRunCoordinator | `workflowRuns[]` |
| Workflow run (designer-local) | PipelineRunCoordinator | metadata `runner: pipeline_run_coordinator` |
| Selection / commands | SelectionContext | projected into `selection` sub-context |
| Agent context tool | workbench:context | will gain mission summary (bounded) |
| Cartography | MapSpec platform | `cartographyProductIds[]` (string refs) |
| Execution scheduling | TaskCenter | never duplicated |

# PLAN — D18 vertical slices

## Slice 0 — Audit (this run)
- [x] BASELINE / AUDIT_WORKFLOW / AUDIT_D14_D17 / DECISIONS / AUTHORITY_MAP

## Slice 1 — MissionContext value type + tests
- [x] mission_context.h/cpp
- [x] CMake (app + test_mission_context + e2e scaffold)
- [x] Round-trip + fingerprint + reject live-pointer fields

## Slice 2 — Consume from selection / one studio
- [x] MissionContextBuilder from SelectionContextSnapshot
- [x] ClassificationStudioWidget input/result ref hooks
- [x] TemporalWorkbenchPanel::exportTemporalContext
- [x] Agent summary JSON helper (`missionSummaryJson`)

## Slice 3 — Workflow document identity in mission
- [x] ActiveWorkflowRef fields + IR2 dock fills them
- [x] E2E scenarios (contract-strength)

## Slice 4 — Mount gaps / publish Results
- [x] Menu/workbench/command mount D14 dual + D15 studio
- [x] Publish Result/Layer into MissionContext
- [x] D17 IR2 production CMake + designer dock
- [ ] DataProjectSerializer / Qgs project XML persistence
- [ ] Guided LabSpec + PipelineRunCoordinator production start
- [ ] Rename IR 2.0 WorkflowDefinition → WorkflowDocument

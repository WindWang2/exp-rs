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
- [ ] Attach IR 2.0 workflowId + fingerprint when designer opens (partial: ActiveWorkflowRef fields exist)
- [x] Stub E2E scenario scaffolding (save/restore + scenario1/3 names)

## Slice 4+ — Mount gaps / publish Results / cartography / full E2E
- Deferred after Slice 1–3 PR

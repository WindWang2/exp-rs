# Implementation review — Undergraduate Lab Cockpit

Date: 2026-09-22 (Asia/Shanghai)

## Architecture

```
data/curriculum + data/labs ──► CurriculumCatalog / Progress / Availability (existing)
                                      │
                                      ▼
                              sicnu_teaching (NEW projection leaf)
                                CourseHomeViewModel
                                LabStepTimeline
                                LabReadiness
                                AutonomyEffectiveDisplay ──► Sicnu::autonomy
                                LabFeedbackProjection
                                LabSessionState (nav refs only)
                                      │
                                      ▼
                              src/app/teaching (NEW UI)
                                CourseHomePage
                                GuidedLabWorkspace
                                LabCockpitDock ──► main_window dock + command
```

## Owns

- `src/teaching/**` — Qt-free projection library
- `src/app/teaching/**` — student UI
- `docs/development/undergrad-lab-cockpit/**`
- `tests/test_teaching_lab_cockpit.cpp`
- Append-only: root CMakeLists, `src/app/CMakeLists.txt`, `tests/CMakeLists.txt`,
  minimal `main_window*` / `command_defs.cpp`

## Does not steal

Parameter/Fault/Debugger Studio, teacher authoring, Agent context broker,
Agent ops/recovery. Operator execution jumps to existing Processing/Guided Workflow
(message stub + signal; no cloned operator UI).

## Test evidence

```
./test_teaching_lab_cockpit
All tests passed (109 assertions in 16 test cases)
```

Coverage mapped to mission items 1–13, 15 + real-repo lab15 e2e + teaching mask.

## Known limits

- Full `sicnu_geo_rs` link not rebuilt in this lane (resource: would compile ~3.4k
  qgis_core objs). Teaching leaf + Catch2 suite built and green.
- Live verifier/grader reports not injected in UI yet — validate button projects
  honest indeterminate (≠ pass) until reports are wired.
- Capsule export is a ref hook (`capsule:pending/<labId>`), not a full builder call.
- Operator jump is an honest stub dialog pointing at existing toolbox.
- Offscreen GUI smoke (item 14) deferred — widgets exist; Catch2 Qt-free preferred.

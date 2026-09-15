# REVIEW_LOG — D18 mount slice

Independent review of D14/D15/D17 mount + publish continuation.

## Architecture
- Mission publish helpers are pure value ops — OK.
- IR2 designer dock is a separate TU from Engine 2.0 pipeline dock — OK (avoids WorkflowDefinition clash).
- Classic georef/classify windows retained — OK (D-I3).

## Lifecycle
- Dual window / studio host use WA_DeleteOnClose=false singletons like I2I — OK.
- Ir2PipelineDesignerDock is a QDockWidget child of main window — OK.

## Workflow / Agent
- ActiveWorkflowRef filled from IR2 dock into m_mission; workbench:context prefers session mission — OK.
- PipelineRunCoordinator still not started from the dock (identity only this slice) — known limitation.

## Tests
- Publish unit tests + E2E scenarios 1–5 contract tests added.
- **Not executed** on agent box (no cmake/g++). Honest EVIDENCE.md.

## Findings disposition
| ID | Sev | Finding | Disposition |
|----|-----|---------|-------------|
| R1 | P2 | IR 2.0 / Engine 2.0 same C++ type name | Deferred rename (D-W1) |
| R2 | P2 | Guided LabSpec / PipelineRunCoordinator not in production yet | Documented D-W2 follow-up |
| R3 | P2 | Classification studio Result is provisional id until path product exists | Acceptable; path publish API ready |
| R4 | P2 | Builds not run on this box | Honest EVIDENCE.md |
| R5 | P1 | D14/D15/D17 previously unmounted | **Cleared** this slice (menus/commands/workbench) |

No new P0. Pre-existing dual WorkflowDefinition name remains P2.

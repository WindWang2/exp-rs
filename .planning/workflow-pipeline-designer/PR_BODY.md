# D17 — Node-based Visual Workflow Pipeline Designer & Agent IR Compiler

Branch `zcode/workflow-pipeline-designer` (worktree `exp-rs-workflow-pipeline-designer`),
base `origin/master` @ `007e70cff6`. ADR: `docs/adr/0162-workflow-ir-v2-and-dag-engine.md`.
Track record: `.planning/workflow-pipeline-designer/` (GOAL/PLAN/DECISIONS/
BASELINE/EVIDENCE/REVIEW_LOG/…).

## What lands

Purely additive (0 deletions vs master; only `.gitignore`,
`src/workflow/CMakeLists.txt`, `tests/CMakeLists.txt` touched among existing
files): a Qt-native **Workflow IR 2.0** designer layer, a **DAG engine**
(Kahn tiers + 3-color DFS cycle diagnosis), a **contract checker + closed
repair engine**, a **plan optimizer** (SHA-256 lineage, DNE, CSE) + **cost
estimator**, an instance-based checkpointed **PipelineRunCoordinator**, a
QGraphicsView **node canvas**, a **guided workbench** (LabSpec cards ⇄
topology over one document), a deterministic **agent orchestrator tool**
(NL→DAG + PROJ/GDAL-log self-healing), and a nine-suite **E2E/test stack**
(807 assertions / 74 cases, all green offscreen).

Five seam paths deviate from the original brief because master occupies
them (IR 1.0, repair 1.0, GuidedWorkflowWidget, WorkflowRunCoordinator);
the mapping table is in `.planning/.../DECISIONS.md` D1.

## Verification

`ninja -j2` builds nine minimal-link test targets (Catch2 + Qt +
jsoncpp; no qgis chain). All suites green with `QT_QPA_PLATFORM=offscreen`:
see EVIDENCE §7 for the exact commands and exit codes, and C-A..C-I for the
capability→command mapping. E2E covers a 100-node/10-tier run under the
1.5 GiB RSS budget, crash consistency (abort at 50 % → resume with the
exact CacheHit prefix), and all 11 shipped `data/labs/*.lab.json` executed
green through the full stack.

## Review

Three read-only subagents (Standards / Spec / adversarial audit) reviewed
the stack; 1×P0 + 7×P1 findings were fixed with regression pins
(`2585aa4904..88c100bcdd`); ~20 P2/P3 fixed or dispositioned with rationale
in REVIEW_LOG.md. No existing production TU was modified.

## Known follow-ups (non-blocking, recorded in REVIEW_LOG)

- ASan/UBSan lane over the new suites (needs the full qgis build).
- Interactive signal wiring of the guided workbench's card host (the
  data-level contracts are tested; the visual shell wiring is next).
- Checkpoint persistence debouncing if embedded on a live UI thread.

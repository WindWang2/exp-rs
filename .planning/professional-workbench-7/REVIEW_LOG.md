# REVIEW LOG

## Track reviews (continuous)

- M1 design review (self): cancel paths must never wait for terminal state →
  `confirmWorkbenchShutdown` fires requestCancel()/cancelTask() and moves on;
  quit remains bounded. Refusal paths (user stays, bench refuses close) abort.
- M2 design review (self): provenance section must resolve only through
  DataManager/WorkspaceService; bounded ancestor walk (depth 6 + visited set);
  deleted assets/deleted layers render warnings, never invented chains.
- M3 design review (self): history is a bounded re-query projection (cap 5000
  + truthful dropped counter) — no second task store; rerun goes through
  TaskCenter::enqueueTask with the same parameter snapshot; resume through
  WorkflowRunCoordinator::resumeRun.
- M5/M6 design review (self): stores opened on user-provided paths (no invented
  project-DB convention); Result diagnostics surfaced verbatim; readiness via
  evaluateRuntimeReadiness only; unknown memory/memory estimates never invented.

## Adversarial review (M8) — planned

Two subagents (track maximum):
1. Architecture/UX review of the shell wiring (shutdown policy + panels).
2. Concurrency/lifecycle review (TaskCenter signal churn vs refresh, paged
   models, deletion races, dock lifetimes).

## Adversarial review (M8) — results + disposition

Reviewer A (architecture/UX): 3 P1, 8 P2, 8 P3. Reviewer B
(concurrency/lifecycle): 0 P1, 5 P2, 7 P3. All P1/P2 fixed in `214536a9`;
notable cheap P3s also fixed. Disposition:

| Finding | Sev | Disposition |
|---------|-----|-------------|
| A1 shortcut double-claim D | P1 | fixed earlier (f3ec3f02, Ctrl+Shift+E); reviewer HEAD predated it |
| A2 panels never addDockWidget | P1 | fixed: docks registered + hidden on demand |
| A3 resumeRun failures dropped | P1 | fixed: statusBar + refresh; comment corrected |
| A4/A-B7 Cancelling unhandled | P2 | fixed: state text, non-terminal, shutdown count |
| A5 icons missing | P2 | fixed: existing aliases |
| A6/A-B3 timeline index space | P2 | fixed: rowForSceneIndex mapping + real min/max + setWindow + hollow unknown markers |
| A7 labelSchemaVersions wrong key | P2 | fixed: read label_schema.schema_id from manifest |
| A8 compare identity pins | P2 | fixed: dataset/fingerprint flags in report |
| A9 §E splits/leakage/readiness absent | P2 | split manifests rendered; rest declared CLI/Agent artifacts |
| A10 device combo misleads | P2 | fixed: labeled "设备筛选" + tooltip |
| A11 first-page truncation | P2 | runs total surfaced; datasets/experiments already showed totals |
| B1 confirmWorkbenchShutdown omits Cancelling | P2 | fixed (merged with A4) |
| B2 coordinator mutex GUI stall | P2 | NOT fixed here — core-side (workflow_run_coordinator I/O under lock); cross-task issue for execution-plane track |
| B4 stale views after store reopen | P2 | fixed: unconditional rebuilds |
| B5 docks not registered | P2 | fixed (same as A2) |
| B6 runStateChanged AutoConnection | P3→fixed | Qt::QueuedConnection |
| B8 dead truncation writer | P3 | fixed: single writer |
| B9 refilter date-parse leak | P3 | fixed: invalid date never passes a window |
| B10 timeline min/max order | P3 | fixed: real min/max |
| B11 item() derefs | P3 | fixed: null-checked |
| B12 cancel-before-confirm order | P3 | fixed: dirty confirmations now precede cancellation |
| A12 output vocabulary duplication | P3 | fixed: isOutputVocabularyKey |
| A13 rerun destination | P3 | fixed: retryTask seam first |
| A15/A16 compare dup + open failure | P3 | fixed: shared helper + status feedback |
| A17 provenance disclosures | P3 | fixed: layer truncation + multi-branch note |
| A18 epoch-0 plotting | P3 | fixed: hollow markers |
| A19 open() creates stores | P3 | fixed: existence guard |
| A20 milestone overclaim | P3 | fixed: honest descope in MILESTONES/FINAL_REPORT |
| A14 artifacts-missing count | P3 | accepted: QA counted elsewhere; documented |

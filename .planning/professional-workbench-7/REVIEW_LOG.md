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

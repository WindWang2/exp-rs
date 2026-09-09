# PLAN — milestones

Small, verifiable increments; each milestone compiles and its tests pass
before the next starts. Commits per milestone.

- M1 (A) Shell lifecycle policy: `workbench_shutdown_policy` + closeEvent/
  project-switch/quit wiring; tests with fake benches (dirty, in-flight,
  cancel-refused, external close refused). Pinned: project close while task
  running, dirty external windows, repeated open/close.
- M2 (B) Provenance section: resolve layer/result/asset → records; lazy +
  cancelPending; tests (layer with derivation record, unknown layer, deleted
  layer in inspector, result id, empty → 无 provenance).
- M3 (C) Processing history: model + panel + actions (retry/rerun/open/
  provenance-jump/compare where seam exists); 100k-row virtual model test;
  cancellation test; states incl. cancelled/interrupted truthfully.
- M4 (D) Temporal workbench panel: paged scene browser + timestep → canvas
  preview + compare hook; large-collection pagination test (synthetic).
- M5 (E) Dataset/Experiment thin clients (read-only first: versions, samples
  paged, splits, leakage, stats, runs, readiness).
- M6 (F) Model bench: catalog + readiness + memory estimate + test-inference
  via runtime seam; error paths verbatim.
- M7 (G/H/I) Deepening: keyboard navigation, warning explanations, UX
  consistency sweep (enabled states, empty/error/loading), registry
  projection of every new action, docs update (docs/ui-architecture.md §13).
- M8 Adversarial review (2 subagents, architecture + concurrency/lifecycle),
  remediation, master sync, PR.

Order Rationale: lifecycle first (protects everything else), provenance+
history share resolution logic, temporal needs the paged-model helper from
M3, benches E/F reuse the same paged helper and section idioms.

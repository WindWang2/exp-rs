# PLAN / MILESTONES

- M0 Contract Inventory — graph + snapshot + integrity test. **Owner: this
  track. Evidence: test_contract_platform_9 green.**
- M1 Canonical Typed Descriptor — `contract_descriptor` + round-trip.
- M2 Operator Contract Projection — scanner + equality test + mutation
  tests (headline; closes #872/#879/#880 class permanently).
- M3 Command/Help/Diagnostics Contract — reference graph + census
  (closes #869/#870/#871/#881/#882 class permanently).
- M4 Capability Knowledge Drift — floors + refs + snapshot discipline.
- M5 Verification Ladder 9.0 — lanes, statuses, JSON, resume (extend 8.0).
- M6 Portability — pure-C++ guards, header probes, no-false-PASS matrix.
- M7 Sanitizer/Fault — bounded ASan lane for contract suites (reuse 8.0
  fault points; not-built reported honestly).
- M8 Benchmark Governance — benchmark schema + contract-cost benchmark.
- M9 Visual/Artifact — reuse 8.0 lanes; contract platform adds artifact
  identity for generated contract JSON (checksums in readiness).
- M10 Release Readiness — collect_readiness contract section + FINAL_REPORT.

Order: M0 → M1 → M2 → M3 → M4 → M5 → (M6, M7, M8) → M10. M9 reuse-only.

Definition of done per milestone: code + tests green locally in this
worktree + planning evidence updated + one structured commit.

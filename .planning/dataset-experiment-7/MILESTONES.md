# MILESTONES

- [x] M0 Build environment in worktree green (configure, build, existing tests)
      — configure-dev.cmd (Ninja, Qt 6.8 + vcpkg manifest, win_flex_bison),
      first full build of test_data_platform_surface running (j2).
- [x] M1 MCP surface (15 tools) + CLI completion
      — src/agent/data_platform_tools.{h,cpp} wired into mcp_server.cpp
      (tools/list + dispatch); CLI verbs dataset list/version/label-schema/
      split/leakage + experiment list/run + reproduce inspect.
      DEVIATION vs PLAN: CLI keeps its jsoncpp projections and calls the same
      store APIs directly instead of a shared projection helper — linking the
      CLI against the GUI-level sicnu_agent SHARED lib (needed for a truly
      shared projection) was rejected as an architecture regression. Drift
      risk accepted: both layers are thin over identical store/library calls.
      ALSO: split manifests + leakage reports gained real persistence
      (dataset_store_splits.cpp) — the audit found runs referencing
      splitManifestIds that could never be resolved from any store.
- [x] M2 Pipeline→SampleRecord promotion
      — src/dataset/sample_promotion.{h,cpp}: classification rules (raw value
      → code, never persisted as identity), segmentation objects, annotation
      chains, pair+event-group, temporal with missing observations. Draft-only
      writes enforced.
- [x] M3 Workflow→ExperimentRun recorder
      — src/experiment/run_recorder.{h,cpp}: startRun (pin verification +
      env redaction + auto fingerprint), markSucceeded/Failed/Cancelled with
      evidence, recordMetrics, reconcileStaleRuns (read-only crash truth).
      Workflow-side call-site wiring deferred to integration (the recorder is
      callable from the existing seams without touching them).
- [x] M4 Fold-level audit
      — src/dataset/fold_audit.{h,cpp}: per-fold materialize + LeakageAuditor,
      per-fold class balance + zero-ratio flags, deterministic replay check
      (content fingerprint equality, note/leakage cleared).
- [x] M5 Facets & quality at scale
      — dataset_store_facets.cpp: sample_facets side table (draft-only, atomic
      replace), SQL-side facetDistribution/facetCrossCounts with bounded
      results + "(other)" tail bucket, facetNames, quality_summaries cache
      with (count, max_roword) staleness stamps.
- [x] M6 Replay readiness
      — src/experiment/replay_readiness.{h,cpp}: per-dependency
      Ok/Missing/Mismatched/Unknown checks, honest level mapping, missing
      diagnostics, equivalentRuns (historical duplicate execution lookup).
- [x] M7 Comparison extensions
      — src/experiment/comparison_ext.{h,cpp}: protocol compatibility (8
      dimensions), schema compatibility (added/removed/foreign verdicts),
      pairedRunComparison (per-class deltas with support gates
      "insufficient_support", no fabricated significance).
- [ ] M8 Build + tests green, adversarial review (≤2 subagents), remediation,
      integration, PR.

Status: library + surface code complete, first build in progress.

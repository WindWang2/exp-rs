# REVIEW_LOG

## Self-review notes (pre-review)

- M1 deviation: CLI and MCP share verbs, not projection code. Reason: a truly
  shared projection would force the CLI to link sicnu_agent (SHARED, pulls
  QGIS GUI). Both layers call identical store/library APIs; duplication is
  format plumbing only.
- Discovery during audit: split manifests and leakage reports had NO
  persistence — runs referenced split ids no store could resolve. Fixed in
  M1 (dataset_store_splits.cpp) with immutability (id→content) and
  append-only report history (digest-keyed).
- `dataset:validate` mutates (stage = the platform's validation procedure,
  same as existing CLI validate). Truthful failure output, not a throw.
  [SUPERSEDED by review F18: MCP validate is now read-only; staging stays a
  CLI management operation.]
- Fold replay check clears note/leakageSummary before fingerprint comparison
  (presentation-only fields), documented in fold_audit.cpp.
- Facet distributions carry "(other)" tail buckets so reported counts always
  sum to total (nothing hidden when the result is bounded).

## Adversarial review round 1 (subagent 1/2, read-only)

Scope: full diff of 0c2b615b. Verdict: no P0; 3 P1; ~12 P2; ~12 P3.

### Fixed (P1)
- F17 reproducibility:validate: presence-only arg check wired a
  `return true` hook even for model_available:false → now honors the value;
  false wires a false hook.
- F18 dataset:validate mutated the store (stageVersion UPDATE) against the
  surface's read-only promise → now a read-only strict-manifest parse
  (valid true/false + diagnostics); staging documented as CLI-only.
- F14 ReplayReadiness: a run with empty algorithmId added NO check and could
  reach Exact → empty algorithm pin is now Missing (blocking).
- F22 (CLI twin of F14) reproduce inspect: hand-rolled level that ignored
  artifacts → now delegates to ReplayReadiness::assess (+ equivalent runs).

### Fixed (P2/P3)
- F1 facet draft-status check moved INSIDE BEGIN IMMEDIATE (#811 playbook).
- F2 setSampleFacets now requires the sample to exist (dataset.sample_not_found).
- F4 saveSplitManifest refuses dangling version refs (dataset.not_found).
- F5 leakageReportsForSplit clamps limit (1..1000).
- F6 saveQualitySummary requires an existing version row.
- F7 promotion: annotation failures return truthful partial-state counts
  (dataset.promotion_partial) instead of a bare failure.
- F8 promoteTemporal refuses conflicting member leakage groups
  (dataset.promotion_group_conflict) instead of silent first-wins.
- F10 dead null statement removed (fold_audit.cpp).
- F11 FoldComparabilitySummary gains replayVerified (Matched/Mismatched vs
  Unverified no longer conflated).
- F12 experiment.store_closed error-code prefix fixed.
- F13 loadRunnable → loadRun; dead null check removed.
- F19 leakage run-mode: persist failure now fails the tool with the reason.
- F21 dataset:version reports true total + truncated flag; inspect mirrors.
- F23 CLI experiment run applies read-time redactSecretKeys (parity with MCP).
- F24 CLI dataset split summarizes by default; assignments opt-in via --limit.

### Test hardening (per review F25–F30)
- Planted-leakage test now pins the exact fold count and the planted digest
  (F25); "(other)" tail bucket exercised with maxValues=1 (F26); cancel
  from Created, terminal double-cancel, and #789 environment-redaction
  probes added (F27); empty-algorithmId Exact-overstatement regression test
  added (F28); direct sampleCount round-trip assertion added (F30).

### Accepted / documented (not fixed)
- F3 "(other)" sentinel collision: accepted — facet values are caller
  evidence; the convention is documented in the tool contract.
- F9/F20 N+1 loops (promoteAnnotations validation, assembleAuditSamples
  annotation lookup): accepted at 100k scale with bounded pages; 1M-scale
  SQL-side join listed as follow-up.
- F15 class codes containing "::" degrade support lookup to unknown
  (support -1 = unknown, benign); F16 nested non-numeric subtrees are
  skipped silently. Both documented as comparison_ext limitations.
- F29 draft-path validate case covered indirectly by the rewritten surface
  test (committed version) + read-only code path is branch-simple.

## Post-fix verification

- test_platform7_library: All tests passed (228 assertions in 15 cases)
- test_data_platform_surface: All tests passed (101 assertions in 5 cases)
- Regression: split_leakage 672/20, dataset_core 155/12,
  experiment_evaluation 162/14, e2e 57/4 — all green.

## Subagent budget

- Round 1: 1 subagent used (read-only review). Second reserved; not needed
  unless integration conflicts require a focused re-review.

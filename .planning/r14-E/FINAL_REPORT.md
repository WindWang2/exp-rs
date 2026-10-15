# R14-E Final Report — 本科遥感实验教学、自动评测与科学证据链收敛

## Summary

Track E audited its full ownership surface against the live master (f72f0c117), deduplicated
every candidate issue against merged PRs' actual code and the open PRs, then delivered nine
real, currently-reproducing fixes with RED→GREEN×2 evidence and two independent review
rounds (round 2: PASS, P0/P1/P2 = 0). A tenth fix (the model-backed studio matrix, WP08)
was completed and verified here but DROPPED at push time when PR #1510 opened on the same
files with the same review thread's bounding arm — the dedupe decision and its evidence are
in LEDGER.md. No new product surface was added; every change hardens an existing
teaching/grading/evidence seam.

## The nine delivered fixes (7 commits: 6 code + 1 docs)

| Fix | Severity | Files |
|---|---|---|
| Batch discovery symlink containment (gradebook could read outside the submissions root; dangling links vanished) | P2 | batch_assessment.cpp |
| Sidecar non-finite accuracy poisoning the passport round-trip | P2 | model_sidecar.cpp |
| Lab session event-seq envelope (forged verdict wins latest-wins gates after reload) | P3 | session_state.cpp |
| Faultlab vacuous "Changed" pass without a clean baseline (text/truth) | P3 | fault_expectations.cpp |
| Faultlab LO-id grammar vs published ^LO-[0-9]{2}$ | P3 | fault_scenario.cpp |
| RecipeRouter data race on concurrent synthesize/refresh (+ torn cache-key snapshot) | P3 | recipe_router.{h,cpp}, broker.cpp |
| Unbounded document slurps → shared bounded reader with typed oversize refusals | P3 | json_util.h, batch_assessment.cpp, script_adapters.cpp, data_pack_manager.cpp |
| Preflight unresolved raster slots → typed unknowns (pair/quality/temporal) | P3 | rules.cpp |
| NFC student-id normalization (Unicode variant split students) | P3 | batch_assessment.cpp |

## Verified ALREADY_FIXED on master (no diff churn — dedupe evidence)

#1466 (all five items), #1464 (both), #1492, #1465's broker cache-key + std::equal×4,
#1488's master-side isUInt64 path. #1488's exception barrier + agent_adapter/capability_router
hardening are owned by OPEN PR #1509 (files untouched). #1458's bundle P1 is #1504's
territory (src/experiment/** untouched). #1465's terminology_provider is src/help/** (outside
E's list; recorded for its owner).

## Residuals (documented, not fixed — see LEDGER.md for reasons)

ISO-8601 grammar divergence grader↔resolver (both fail closed in-domain); ASCII-only leak
regex in feedback_pack; status-counter reconciliation wrinkle; spec_runtime authoring gap;
hidden-link visible-refusal nuance; router move-assignment lock-order hardening.

## Evidence index

- BASELINE.md — remote state, open-PR dedupe table, issue triage with code citations
- ORACLES.md — 10 oracles, failing inputs, RED types
- TEST_RESULTS.md — RED transcripts summary, GREEN×2 runs (84/84 twice + confirm), 19 ms
  10k-row measurement
- REVIEW.md — round 1 (FAIL → all P0/P1/P2 resolved) and round 2 (PASS) records
- QDIR_FILTER_PROBE.txt — the empirical Qt 6.12 filter probe behind allEntries()
- OWNERSHIP.md / LEDGER.md — written paths + full 16-WP status matrix

## Why no CI wait

Per the R14 protocol: local offline verification only; no GitHub Actions results are
awaited or claimed. Build/test commands and real exit codes are in TEST_RESULTS.md.

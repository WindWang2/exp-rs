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
- MCP `reproducibility:inspect` delegates to ReplayReadiness (single
  implementation, library-tested); unwired model/algorithm hooks stay
  "unknown" → BestEffort, never a fabricated Exact.
- Paired comparison reports deltas + supports with explicit
  "insufficient_support" flags; no significance claims anywhere (goal §G).
- Fold replay check clears note/leakageSummary before fingerprint comparison
  (presentation-only fields), documented in fold_audit.cpp.
- Facet distributions carry "(other)" tail buckets so reported counts always
  sum to total (nothing hidden when the result is bounded).

## Subagent reviews

- (pending) Round 1 after build+tests green.

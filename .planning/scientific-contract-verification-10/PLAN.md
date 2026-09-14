# PLAN — scientific-contract-verification-10

## Work packages

| ID | Package | Key deliverables |
|---|---|---|
| A | Findings digestion: fix the 7 whole-repo-review findings on current master + regression tests | F-OPS-4 (P1) sourceCrsOverride plumbed through WarpOptions/warpRaster/IoReprojectOperator; F-OPS-1 class_mapping↔encoding gate (catalog validation + engine encoding selection); F-OPS-3 qa_mask fail-closed; F-OPS-5 cancelable + spatially bucketed NMS; F-OPS-2 fromMat ND non-contiguous copy; F-PI-1/F-PI-2 bridge convergence (overflow→kill, deadline finally, shared anti-drift test). Real Catch2 tests replacing `review/tests/` drafts; node test for pi. |
| B | Scientific Contract Registry (`src/contracts/scientific_contract.*`) | One machine-readable record per first-party `rs:` operator: modality, numeric domain, scale/offset, band roles, CRS/grid/time, NoData/mask semantics, categorical encoding, class-id range, determinism grade, seed, cancellation granularity, memory profile, atomic publication, provenance, failure codes. Completeness enforced against the live registry (every registered id covered; no phantom ids). Cross-checks vs `stampDeterminismGrade` values. |
| C | Cross-projection drift gates (`tests/test_drift_projection_10.cpp` + scanner extension) | Mechanical equality/drift checks across: live descriptors ↔ `data/processing/algorithm_meta/**` ↔ capability knowledge ↔ help parameter knowledge ↔ recipes ↔ LabSpec references ↔ workflow contract surfaces. Missing projection → loud finding with OWNED-BY marker policy. |
| D | Verification platform 10.0 (`tests/test_science_verification_10.cpp` + ladder lanes) | Metamorphic scientific invariants (known-answer extension), seed-determinism replay, provenance verification of a generated asset, atomic-output failure matrix extension, sanitizer-friendly smoke lane, deterministic bounded fuzz where a parser surface is newly touched. |
| E | Readiness refresh at final HEAD | `scripts/collect_readiness.py` run at final SHA → `docs/verification/READINESS.{md,json}` refreshed; honest passed/failed/skipped/timeout/not-built verdicts. |
| F | Independent review + fixes | ≤2 read-only subagents (architecture/science lens; concurrency/test-trust lens); P0/P1 → fix; P2 high-value → fix; disposition ledger in REVIEW_LOG.md. |
| G | Final verification + PR | Targeted builds/tests at final HEAD, `git diff --check`, conflict-marker scan, snapshot freshness, evidence files, push, `gh pr create` (no merge). |

## Execution order

A (unblocks nothing, largest risk first: P1) → B (authority decision) → C (needs B) → D (needs A's tests as anchors) → E → F → G.

## Budget (300M tokens nominal)

| Phase | Content | M |
|---|---|---:|
| 0 | Baseline/dedupe/planning | 18 |
| 1 | WP-A findings P1+P2 fixes | 54 |
| 2 | WP-B scientific registry | 54 |
| 3 | WP-C drift gates | 46 |
| 4 | WP-D verification upgrades | 38 |
| 5 | WP-E readiness + docs | 20 |
| 6 | WP-F review | 24 |
| 7 | WP-F fixes | 20 |
| 8 | WP-G final + PR | 26 |
| **Σ** | | **300** |

Budget is measured in tool calls / files touched per phase (no token counter in session), logged in EVIDENCE.md; >1.5× envelope → budget line + continue.

# TEST_MATRIX

Every row = a new or extended automated test with local evidence.

## M1 — MCP/CLI surface
- [ ] Each of the 15 MCP tools: happy path returns bounded JSON; `limit` respected; unknown id → typed error, not crash.
- [ ] Pagination: `next_offset` correct across page boundaries; `limit` over store max clamped.
- [ ] `reproducibility:export` writes bundle + checksums; secret scan of all emitted files (denylist names/values absent).
- [ ] CLI verbs mirror MCP outputs (same projection helper), exit codes truthful.

## M2 — promotion
- [ ] Classification raster (synthetic, 3 classes + nodata) → expected polygon/pixel samples; class codes come from LabelMapping, never row index (mapping permutation test).
- [ ] Segmentation objects → ObjectSample with asset+objectRef+bounds.
- [ ] Annotation chain: append revisions, parent linkage enforced; pseudo labels carry model id+digest+threshold.
- [ ] Pairs share eventGroup; temporal samples keep missing observations.
- [ ] Promotion into committed version refused (`dataset.not_draft`).
- [ ] Transaction/crash: batch insert abort leaves no partial samples (existing store semantics, exercised through promotion).

## M3 — run recorder
- [ ] created→running→succeeded records all identity pins; config/exec/result fingerprints distinct and stable.
- [ ] Failure path: exception → run Failed with error evidence; cancel → Cancelled; neither reported as success.
- [ ] Stale `Running` run (simulated crash) surfaces in reconcile report; no auto-success.
- [ ] Transition violations rejected (`experiment.bad_transition`).
- [ ] Environment snapshot is allowlisted + secret-filtered (denylist probe).

## M4 — folds
- [ ] Planted duplicate across fold k's test and fold j's train → finding with fold evidence.
- [ ] Fold comparability: per-fold sizes/class counts computed deterministically; zero-ratio (class absent in a fold) flagged.
- [ ] Deterministic replay: `SplitEngine.generate` twice + fingerprint equality; different seed → different fingerprint.

## M5 — facets/scale
- [ ] 100k synthetic samples: facet counts sum to total; per-facet filters correct; memory bounded (paged; peak RSS smoke check via test design — no full materialization).
- [ ] Incremental quality cache: detectable staleness after draft mutation; committed version summary stable.
- [ ] Duplicate summary: exact-digest duplicates counted; digest-unknown quantified (never silently "clean").

## M6 — replay readiness
- [ ] Missing dataset version / split / model / artifact each produce a MissingDependency diagnostic; level degrades honestly (Exact only when everything pinned+available).
- [ ] Fingerprint mismatch (same id, different content) → Impossible with evidence.
- [ ] Bundle secret scan + checksums verified (extends existing tests).

## M7 — comparison
- [ ] Known comparable pair (identical pins) → Comparable; differing dataset version → NotComparable; differing config only → ComparableWithDifferences.
- [ ] Protocol compatibility: ignore-labels/aggregation/threshold differences detected; identical → compatible.
- [ ] Schema compatibility: class added → compatible-with-difference; class removed → not comparable; renamed → detected.
- [ ] Paired runs: per-subset deltas with counts; n < threshold ⇒ "insufficient_n", no significance claim.

## Regression guards
- [ ] All existing dataset/experiment tests stay green after every milestone.
- [ ] Build with `CMAKE_BUILD_PARALLEL_LEVEL=2`, tests with `CTEST_PARALLEL_LEVEL=1`.

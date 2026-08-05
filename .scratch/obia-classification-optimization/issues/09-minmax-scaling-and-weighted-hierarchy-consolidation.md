# 09 — MinMax Feature Normalization & Probability-Weighted Hierarchy Consolidation

**What to build:** Expand `RsFeatureScaler` with MinMax normalization mode alongside Z-score, and add `ProbabilityWeightedVote` mode to `RsHierarchyClassConsolidator`.

**Blocked by:** 06 — Standards & Code Quality Cleanup

**Status:** resolved

- [ ] Add `MinMax` normalization mode to `RsFeatureScaler`
- [ ] Add `ProbabilityWeightedVote` strategy to `RsHierarchyClassConsolidator`
- [ ] Update `runHierarchyConsolidation` dialog options in `RsObiaMainWindow`
- [ ] Write Catch2 test for MinMax scaling and weighted hierarchy consolidation
- [ ] Build and pass all Catch2 unit tests

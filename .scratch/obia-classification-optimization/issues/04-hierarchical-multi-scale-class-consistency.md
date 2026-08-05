# 04 — Hierarchical Multi-Scale Classification Consistency Consolidation

Type: task
Status: resolved
Blocked by: 01, 02

## Question

Independent level-by-level classification in multi-scale `RsObjectHierarchy` can lead to inter-level class contradictions (e.g. fine-level sub-segments classified as Water while the coarse parent is classified as Building).

How should `RsHierarchyClassConsolidator` perform top-down or bottom-up class voting and probability-weighted parent-child constraint enforcement across `RsObjectHierarchy` level segment maps?

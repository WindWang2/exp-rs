# Deterministic splits

`SplitEngine::generate(config, versionId, inputs)` is a pure function
(ADR 0136): identical (config, seed, inputs) replay byte-identically on
every platform - the PRNG (splitmix64 + PCG-style + Fisher-Yates) is fixed
in `deterministic_random.h`, independent of std::mt19937 semantics.
Per-purpose seed derivation (`seedFor`) keeps new stochastic steps from
perturbing existing ones.

Methods: random, stratified (per-class), grouped, spatial_block,
spatial_buffer (greedy seeded test selection + exclusion radius),
temporal (time-ordered whole groups), k_fold, spatial_k_fold, group_k_fold,
leave_one_region/scene/year_out.

Output: persisted `SplitManifest` (id, config, determinism grade, one
`SplitAssignment` per sample: role and/or fold). Fold methods store folds
(role Unassigned) and materialize via `materializeFold(i)` (fold -> Test,
rest -> Train). Every manifest carries a canonical fingerprint; equal
fingerprints = identical assignments.

The legacy `RsClassificationSplit` (analysis layer) remains for the
in-process cv::Mat path; nothing existing changed.

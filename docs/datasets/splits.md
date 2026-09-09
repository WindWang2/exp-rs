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

## Foundation 6.0 ratio and atomicity contracts (#775, #786, #788)

- **Spatial blocks are atomic units**: `spatial_block` walks the shuffled
  block list with the same whole-group budget walk as `grouped` — every
  sample inside one block grid cell carries the same role. Splitting inside
  a block (the pre-6.0 behavior) is train/test leakage by construction.
- **Ratio targets use the Hare-Niemeyer largest-remainder distribution**
  (`largestRemainderCounts`), with deterministic Train > Validation > Test
  tie-breaks. A zero ratio is a hard contract: `test_ratio: 0` can never
  receive samples, and a non-zero ratio is never starved to zero by floor
  rounding (the pre-6.0 behavior dumped every remainder into Test).
- **Grouped walks**: groups that overflow both budgets land on the
  nonzero-ratio role furthest under its target — a zero-ratio role never
  receives overflow. A non-zero-ratio role that still ends up empty (one
  giant group) is a typed refusal, not a silent manifest.
- **Spatial buffer**: buffer-vetoed samples are excluded from the
  train/validation remainder entirely (role `Unassigned`); the pre-6.0
  behavior counted them in the remainder and starved Validation to zero.
- **Block isolation is asserted, not assumed**: `test_split_leakage` checks
  that every sample of a spatial block shares one role.
- **Leakage audit spatial hashing is injective** (`QPair<qint64,qint64>`
  cell keys): scenes straddling the coordinate axes no longer merge
  unrelated cells into one bucket (duplicate findings / wrong-neighbor
  walks, #787).

# SPLIT_MODEL

Authority: `src/dataset/split.h` + `leakage_audit.h` (ADR 0136). **Do not fork.**

D19 uses existing methods for scientific modes:

| Benchmark mode | Prefer SplitMethod |
|----------------|--------------------|
| random baseline | Random / Stratified |
| spatial isolation | SpatialBlock / SpatialBuffer |
| cross-region | LeaveOneRegionOut / Grouped |
| cross-scene | LeaveOneSceneOut |
| cross-year | LeaveOneYearOut / Temporal |
| cross-sensor | Grouped on sensor facet + holdout |
| space×time | SpatioTemporalBlock |

Every split cited by a benchmark must be a **stored** SplitManifest. Leakage audits produce PASS/WARN/FAIL/UNKNOWN via Foundry mapping — never a silent boolean "independent".

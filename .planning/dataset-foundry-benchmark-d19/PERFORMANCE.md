# PERFORMANCE — D19

- Sample catalog: hard page cap 500 (aligned with DatasetStore::kMaxPageSize).
- Hermetic scale stress: **N=100000** flat `SampleCatalogRow`s; filter/page/summary
  must not return more than `limit` (≤500) rows. 1M skipped on ~4 GiB MemAvailable boxes.
- Feature join findings capped (`maxFindings`, default 200).
- QA report is pure assembly over caller evidence — no full-dataset materialization.
- Benchmark runner is O(predictions) hash joins; confusion matrix size = |labels|².
- No GUI tests in this track.

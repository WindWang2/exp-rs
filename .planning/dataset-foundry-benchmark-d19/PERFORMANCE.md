# PERFORMANCE — D19

- Sample catalog: hard page cap 500 (aligned with DatasetStore::kMaxPageSize).
- Feature join findings capped (`maxFindings`, default 200).
- QA report is pure assembly over caller evidence — no full-dataset materialization.
- Benchmark runner is O(predictions) hash joins; confusion matrix size = |labels|².
- No GUI tests in this track.

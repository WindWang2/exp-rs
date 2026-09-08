# Performance & bounded memory

Contracts (enforced by test_dataset_quality_scale):
- 100k sample ingest in 1k batches (single transactions);
- full scan via 500-row pages - constant memory, monotonic order, stable
  first row on re-read;
- grouped split + leakage audit over 100k rows complete in seconds-scale
  time (timings recorded as evidence, never gate on machine speed);
- no API materializes all samples; `sampleGroupIds` is capped and ordered.

Design: SQLite indexes on (dataset_version, roword) and group;
spatial audits bucket by x-cell; identity checks hash by key. Fingerprint
computations hash METADATA only - they never walk pixel payloads.

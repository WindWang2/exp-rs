# R16-C/T06 — performance notes

The four fix commits make **no new performance claims**; their messages contain
only RED/GREEN test counts, no timings or batch-scale measurements.

What was verified afresh on final HEAD:

- The existing batch scale oracle —
  `test_teaching_admin_core::batch scale oracle: 1000 mixed outcomes stay
  deterministic and durable` (`tests/test_teaching_admin_core.cpp:1951`,
  1000 submissions, `maxConcurrency = 4`, two full runs asserting identical
  report digests and a durable 1000-row checkpoint) — is **green in both
  verifier family runs** (64/64 ×2; see `TEST_MATRIX.md`).
- WP1's stamp change adds a bounded (first 64 KiB) sha256 per graded artifact
  on the resume fast path — a deliberate cost/robustness trade recorded in the
  commit, not a measured regression; the 1000-scale oracle above ran well
  within the family's ~5.2-5.4 s total wall time both runs (observed, not a
  benchmark).
- WP2 bounds the transcript read by the existing `maxBytes` budget; WP3 adds
  O(1) per-criterion finite/non-negative checks; WP4 is a per-row assignment.
  None introduce new allocation or I/O loops.

Bottom line: no new perf claims; the existing 1000-mixed-outcome batch scale
oracle is green in both final-HEAD family runs.

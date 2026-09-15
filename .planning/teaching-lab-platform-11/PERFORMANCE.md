# PERFORMANCE — resource model & evidence

- Envelope: ninja -j2 (CMAKE_BUILD_PARALLEL_LEVEL=2), ctest -j1, offscreen Qt.
- Grading: windowed streaming, per-assertion budget (--max-bytes, default 64 MiB);
  memory bounded by largest single artifact, not class size (D7 contract).
- Batch: memory O(1 submission); summary files written atomically per run.
- Scale evidence: (pending Phase 5 — RSS/queue/logical-scale numbers recorded here)
- Compile-time CPU/RSS sampling: (recorded per phase; if not measurable on this
  host, recorded once here and -j2 cap kept per GOAL)

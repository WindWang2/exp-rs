# PERFORMANCE — resource model & evidence

- Envelope: ninja -j2 (CMAKE_BUILD_PARALLEL_LEVEL=2), ctest -j1, offscreen Qt.
- Grading: windowed streaming, per-assertion budget (--max-bytes, default 64 MiB);
  memory bounded by largest single artifact, not class size (D7 contract).
  Grader 2.0 kernels hold up to THREE windows simultaneously (artifact + zones
  + reference/truth), each planned within the same budget — same precedent as
  the D4 classification walk (artifact + truth); worst case 3 x maxBytes per
  kernel, documented in GRADING.md.
- Batch: memory O(1 submission); summary files written atomically per run.
- Scale evidence (measured 2026-09-16, host = dev workstation, Release build):
  100-submission gate: pass (1665 assertions); 1000-submission opt-in run:
  pass (9819 assertions) with 19/19 content-duplicates detected; memory stays
  O(largest artifact) by construction (streaming hash + per-submission CSV
  flush); summary rows are tiny text (documented in lab_batch_runner.h).
- Compile-time sampling: ninja -j2 on the shared workstation; per-second RSS
  sampling not scriptable here without extra tooling (recorded once per GOAL
  fallback); the -j2 cap was kept for every build in this track.
- Compile-time CPU/RSS sampling: (recorded per phase; if not measurable on this
  host, recorded once here and -j2 cap kept per GOAL)

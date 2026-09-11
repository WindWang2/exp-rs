# PERFORMANCE & RESOURCE EVIDENCE — scientific-mlops-9

Environment: Ninja Release, GCC (see BASELINE), builds -j4 (policy cap;
three sibling 9.x tracks building concurrently on the same 16-core host),
tests -j1/-j2. All numbers from this worktree, Release build, and measured
under that documented load — absolute numbers are therefore upper-biased;
the asserts gate bounds, not speed records. Debug/Release numbers are never
compared.

| Area | Suite/case | Scale | Measured | Stated bound |
|---|---|---|---|---|
| Run seeding | test_mlops9_scale "100k-run store" | 100 000 single-row transactions | seed_ms = 23 286 | (context, not gated) |
| Paged listing | same | 5 pages × ≤500 runs | five_pages_ms = 57 | < 5 000 ms |
| Cold-path execution-ref scan | same | 100 000 run JSONs, substring scan | execution_ref_scan_ms = 71 602 (~0.72 ms/run under 3-track load) | < 120 000 ms; documented linear cold path; O(log n) column+index = follow-up for the store owner |
| Concurrent readers/writer | test_mlops9_scale "concurrent readers" | 400 writes vs 2 spinning readers | all reads/writes succeed; final count exact | correctness gate |
| CLI E2E | test_mlops9_cli_record (3 cases) | real CLI subprocess ×3 | suite < 60 s incl. 3 process starts | TIMEOUT 300 |
| Matrix aggregation | test_mlops9_evidence | 4 cells over real store | < 1 s | bounded by cell cap (≤1000) |
| Split generation | test_mlops9_split | ≤3000 samples/case | suite < 5 s | pure function, O(n) per method (SpatialBuffer accept-loop is O(n·k) in accepted tests — documented) |

## Resource bounds stated in code

- `kMaxMatrixCells = 1000` — sweep descriptors above the cap are refused
  (`experiment.matrix_too_large`), never truncated.
- `kSplitSummaryMaxClasses = 256` — per-role class distribution caps with an
  explicit `class_distribution_truncated` flag.
- `kMaxVersionLineageDepth = 256` — lineage walks refuse loops/unbounded
  depth (`dataset.version_cycle`).
- Split manifests bound their summary; experiment metadata bounds steps
  (≤256, 8.0) and pages (kMaxPageSize = 500, both stores).

# REVIEW_LOG — model-runtime-multimodal-9

Review passes: (1) main-agent self-review during/after implementation,
(2) read-only Reviewer A (architecture/correctness/concurrency/scientific
validity/security), (3) read-only Reviewer B (tests/performance/
portability/resource bounds/docs-vs-code). All P0/P1 fixed; P2/P3 fixed
or dispositioned below.

## Self-review (main agent)

| # | Sev | Finding | Disposition |
|---|---|---|---|
| S1 | P1 | feather + non-grid-preserving head (strided export or resize_to_input) would bypass the accumulator and write tiles directly — two write paths over the same pixels | FIXED (commit after 2cb5dcc4): per-(head,tile) typed InvalidParameter refusal; edge tiles make fed sizes vary per tile, hence per-tile verdict |
| S2 | P2 | feather requires halo>0 but nothing demanded grid-preserving geometry up front | covered by S1's per-tile refusal (geometry known only after the first forward) |
| S3 | P2 | blend refusal for runMultiInput is implemented; docs updated to call it a 9.0 limitation | FIXED in docs (platform-9.md, ADR 0144) |
| S4 | P3 | uncertainty band under feather blends per-tile entropy values (weighted) rather than recomputing entropy of blended probabilities | accepted: entropy-of-average vs average-of-entropy; documented choice keeps band definition per-tile-stable; revisit with a per-pixel online entropy estimator |

## Environmental flake investigation (recorded for honesty)

One full-suite batch run on a machine with three parallel track builds
active produced SIGABRT/heap-corruption symptoms in three suites
(publish-path messages, "free(): invalid pointer"). Investigated:
- baseline (pristine origin/master operators) re-run: green 2x each;
- my sources re-run on the quiet machine: green across all 13 suites
  (twice), plus MALLOC_PERTURB_=170 green 3x on test_model_runtime_8;
- /tmp (tmpfs) was under heavy parallel allocation at the time.
Conclusion: load-environment flake, not reproducible from these changes;
kept under observation. The final clean sweep (frozen sources, quiet
machine) is the recorded evidence.

## Pre-existing (not this track)

| Finding | Disposition |
|---|---|
| test_capability_drift: uncovered operators rs:rasterize / rs:zonal_stats / rs:sar_geocode / rs:sar_temporal_stats | pre-existing harness-knowledge coverage gap (8.0 review noted it as pre-existing on master); scientific operators are other tracks' ownership. No model operators are uncovered by this branch. |

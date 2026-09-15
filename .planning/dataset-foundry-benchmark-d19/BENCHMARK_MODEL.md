# BENCHMARK_MODEL

A `BenchmarkDefinition` pins:

- benchmark_id + benchmark_version (immutable when published)
- dataset_version_id + split_manifest_id + label_schema ref
- task family (classification, segmentation, change_detection, object_detection, regression, temporal_prediction, spectral_matching)
- metric names (resolved through existing evaluation formulas — no duplicates)
- evaluation rules (subset, ignore labels, thresholds) → embeds/aligns EvaluationProtocol
- allow/forbid preprocessing notes (data, not scripts)
- refusePseudoLabelsInTest (default true)
- seed + determinism policy
- environment pin requirements (software revision, provider, device — recorded if present)

`BenchmarkRunner` is headless: definition → validate pins against DatasetStore → optional pseudo-label gate → accept predictions → compute metrics via evaluation.* → record ExperimentRun linkage → emit BenchmarkResult evidence.

Comparisons: absolute/relative delta, per-class delta, mean/std across seed replicates — without claiming statistical significance unless assumptions are documented.

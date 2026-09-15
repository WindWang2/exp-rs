# PLAN — D19 execution order

1. Audit + planning docs (BASELINE, AUDIT_DATASET, DECISIONS, …)
2. DatasetRole on Manifest + vocabulary
3. FeatureSet + identity join
4. SampleCatalogQuery + DatasetQaReport
5. DatasetFoundryService
6. BenchmarkDefinition + Runner + Compare + BenchmarkService
7. Experiment pin field for benchmark_definition_id (append-only if needed)
8. Unit/integration tests + CMake wiring
9. Evidence (honest not-executed if no toolchain)
10. Commit / push / PR (do not merge)

Out of scope this slice: Workbench UI, full annotation GUI, new GDAL I/O, second split engine, online CI.

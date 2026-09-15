# ARTIFACT_MATRIX

| Artifact | Owner | Mutable? | Digest? |
|----------|-------|----------|---------|
| DatasetManifest | dataset | draft only | yes (fingerprint) |
| SampleRecord | dataset | draft only | payload hash optional |
| AnnotationRevision | dataset | append-only chain | tip is current |
| LabelSchema | dataset | immutable per (id,ver) | fingerprint |
| SplitManifest | dataset | write-once | fingerprint |
| LeakageReport | dataset | append-only | content digest |
| FeatureSet | dataset (D19) | write-once schema | digest |
| BenchmarkDefinition | experiment (D19) | versioned immutable | digest |
| BenchmarkResult | experiment (D19) | write-once | result fingerprint |
| ExperimentRun | experiment | lifecycle FSM | exec/result fingerprints |
| MetricRecord | experiment | bound to protocol | metricsHash |

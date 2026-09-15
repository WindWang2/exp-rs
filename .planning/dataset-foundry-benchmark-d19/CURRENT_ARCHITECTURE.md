# CURRENT_ARCHITECTURE — D19 target layering

```
sicnu_dataset (science core, Qt Core + Sicnu::data + SQLite private)
  DatasetStore / Manifest / Version DAG          [EXISTING — extend role]
  Sample / Annotation / LabelSchema              [EXISTING]
  Split / Leakage / Patch / Promotion / Quality  [EXISTING]
  FeatureSet + identity join                     [NEW]
  SampleCatalogQuery (bounded filters)           [NEW]
  DatasetQaReport (multi-category verdicts)      [NEW]
  DatasetFoundryService                          [NEW façade]

sicnu_experiment (depends Sicnu::dataset)
  EvaluationProtocol / metrics                   [EXISTING — reuse]
  ExperimentStore / Run / Promotion              [EXISTING — link]
  BenchmarkDefinition                            [NEW]
  BenchmarkRunner (headless)                     [NEW]
  BenchmarkCompare                               [NEW]
  BenchmarkService                               [NEW façade]

D18 Workbench (out of scope)
  consumes FoundryService / BenchmarkService later
```

Data flow for a scientific benchmark:

```
DatasetVersion (frozen) + SplitManifest + LabelSchema
        ↓
BenchmarkDefinition (task, metrics, policies, pins)
        ↓
BenchmarkRunner → predictions (caller/model adapter) → EvaluationProtocol metrics
        ↓
ExperimentRun + MetricRecord + optional PromotionEvidence
```

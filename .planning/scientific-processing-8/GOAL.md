# GOAL — Scientific Remote Sensing & Geospatial Algorithms 8.0

Advance ExpRS from a broad remote-sensing operator collection to a deeply
validated scientific processing platform with rigorous SAR geometry, temporal
analysis, raster-vector analytics, and single-source scientific contracts,
while preserving the kernel -> RSOperator -> workflow architecture.

Non-negotiable architecture laws (see BASELINE.md/OWNERSHIP.md for how this
track honors them):

- Pi is the single agent loop; no second agent framework.
- Workflow execution stays `WorkflowRunCoordinator -> TaskCenter -> JobEngine -> Executor/RSOperator`.
- `RSOperatorRegistry` is the authoritative operator surface (new operators register there).
- Model execution stays behind `IModelRuntime` / `runModelInference`.
- QGIS stays the authoritative renderer; new kernels are GUI-free.
- Geospatial I/O stays in `src/geospatial/**`; kernels consume it.
- Persistence stays `DatasetStore` / `ExperimentStore`.
- New registries/managers require proof that no authoritative equivalent exists.

Workload class: 3e8+ token goal — depth, validation, and correctness first;
feature count is explicitly not the optimization target.

Subagent budget: at most 2 total (adversarial review only).
Online CI/CD: not required; local reproducible evidence only.

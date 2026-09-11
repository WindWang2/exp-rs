# GOAL — Verification, Portability, Observability & Release Engineering 8.0

Turn ExpRS's post-7.0 verification layer into a production-grade,
cross-platform, deterministic quality and release engineering platform that
catches integration, ABI, scientific, lifecycle, and performance regressions
before they reach master — without changing product semantics or creating a
second execution architecture.

## Non-negotiable laws (restated)

- Pi stays the single agent loop; no second agent framework.
- Workflow execution stays `WorkflowRunCoordinator -> TaskCenter -> JobEngine
  -> Executor/RSOperator`; no parallel scheduler.
- `RSOperatorRegistry` remains the operator surface.
- Model execution stays behind `IModelRuntime` / `runModelInference`.
- QGIS remains the rendering authority; geospatial I/O stays in
  `src/geospatial/**`; persistence stays `DatasetStore` / `ExperimentStore`.
- Trace/fault infrastructure stays inside `src/runtime/observability/**` and
  is applied at existing seams as thin additive adapters — never a second
  event bus.

## Evidence policy

No online CI. Every capability claim in the final report maps to a local
command + exit code, or is explicitly marked not-compiled / not-executed.

## Branch

`feat/verification-platform-8` from `origin/master` =
`322dfd3876c34ed62b42846598cacd711c8c91d6`.

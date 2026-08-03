# 04 — Migrate TaskCenter Descriptors and Remove Runtime AlgorithmEngine APIs

**What to build:** Update `TaskCenter` and `agent_workflow_executor` to resolve algorithm descriptors and resource profiles directly from `AtomicAlgorithmRegistry` (using `sicnu::processing::AlgorithmDescriptor`). Delete runtime lookup/execution APIs (`findAlgorithm`, `executeAlgorithm`, `validateParameters`, `registerAlgorithm`, `registerProcessingAlgorithm`) and `sicnu::AlgorithmDescriptor` from `AlgorithmEngine`. `AlgorithmEngine` is now strictly a startup Provider discovery host.

**Blocked by:** 03 — Direct Provider Registration to Atomic Algorithm Registry

**Status:** ready-for-agent

- [ ] Update `TaskCenter` algorithm descriptor resolution to call `AtomicAlgorithmRegistry::instance().findAdapter()`.
- [ ] Remove `findAlgorithm`, `executeAlgorithm`, `validateParameters`, `registerAlgorithm`, `registerProcessingAlgorithm`, `populateFromProcessingRegistry`, `clear`, and `sicnu::AlgorithmDescriptor` from `AlgorithmEngine`.
- [ ] Consolidate all callers to use `sicnu::processing::AlgorithmDescriptor`.
- [ ] Project builds cleanly and full test suite passes.

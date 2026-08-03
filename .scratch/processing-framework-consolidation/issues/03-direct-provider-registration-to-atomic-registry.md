# 03 — Direct Provider Registration to Atomic Algorithm Registry

**What to build:** Update `AlgorithmProviderAdapter::discoverAlgorithms` to register `ProviderAlgorithmAdapter` instances directly into `AtomicAlgorithmRegistry`. Remove `QgsProcessingAlgorithmAdapter`, `TaskAlgorithmAdapter` interface, and the ADR 0012 mirroring callback (`setProviderAlgorithmProvider`).

**Blocked by:** 02 — Migrate Python Algorithm Adapters & Bridge to Atomic Algorithm Registry

**Status:** ready-for-agent

- [ ] Change `AlgorithmProviderAdapter::discoverAlgorithms` parameter/behavior to populate `AtomicAlgorithmRegistry` directly.
- [ ] Implement direct registration in `QgsProcessingProviderAdapter` creating `ProviderAlgorithmAdapter` instances.
- [ ] Delete `QgsProcessingAlgorithmAdapter`, `TaskAlgorithmAdapter` interface, and `setProviderAlgorithmProvider` mirror callback.
- [ ] Project builds cleanly and algorithm discovery tests pass.

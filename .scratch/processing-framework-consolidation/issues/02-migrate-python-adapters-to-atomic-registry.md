# 02 — Migrate Python Algorithm Adapters & Bridge to Atomic Algorithm Registry

**What to build:** Rewrite `PythonAlgorithmAdapter` to implement `AtomicAlgorithmAdapter` (using Json::Value typed parameter handoff) and update `app_interface_bridge` and `python_plugin_host` to register and execute Python algorithms directly via `AtomicAlgorithmRegistry::instance()`. This eliminates the sole production caller of `AlgorithmEngine::executeAlgorithm`.

**Blocked by:** None — can start immediately.

**Status:** resolved

- [x] Refactor `PythonAlgorithmAdapter` to inherit from `AtomicAlgorithmAdapter` instead of `TaskAlgorithmAdapter`.
- [x] Update `PythonProcessingProviderAdapter` and `app_interface_bridge` to register Python algorithm adapters into `AtomicAlgorithmRegistry`.
- [x] Update `python_plugin_host` execution to call `AtomicAlgorithmRegistry::instance().findAdapter(algoId)->execute(...)`.
- [x] Project builds cleanly and Python plugin host tests pass.

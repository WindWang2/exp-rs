Status: ready-for-agent

## Problem Statement

The `processing/framework/` layer carries two parallel algorithm registries — `AlgorithmEngine` (Qt/QVariantMap interface, ADR 0007) and `AtomicAlgorithmRegistry` (Json::Value interface, ADR 0012) — that register the same batch of algorithms through different adapter wrappers. Every provider algorithm discovered at startup is mirrored from `AlgorithmEngine` into `AtomicAlgorithmRegistry`, doubling memory and creating two divergent lookup paths for the same concept. `AlgorithmEngine::executeAlgorithm()` has exactly one remaining production caller (`python_plugin_host`), while every other execution path (Agent, CLI, GUI via TaskCenter/JobEngine) runs through `AtomicAlgorithmRegistry`.

Additionally, three modules in `processing/framework/` — `ErrorReporter`, `ProcessingCache`, and `ProgressCallback` (virtual base class) — have zero production callers and exist only as compiled dead weight.

## Solution

1. **Collapse `AlgorithmEngine`'s runtime role** into `AtomicAlgorithmRegistry`. `AlgorithmEngine` retains ownership of startup-time Provider discovery (`initialize()`, `registerProvider()`, provider management), but its runtime lookup/execute methods (`findAlgorithm`, `executeAlgorithm`, `validateParameters`) are removed. All runtime callers migrate to `AtomicAlgorithmRegistry`.

2. **Delete dead modules**: `ErrorReporter`, `ProcessingCache`, and `ProgressCallback` (the virtual base class and `SimpleProgressCallback`), along with their tests.

## User Stories

1. As a maintainer, I want a single algorithm registry to consult when debugging algorithm lookup failures, so that I don't have to check two registries with different adapter types to find where an algorithm is registered.
2. As a maintainer, I want Provider discovery to happen exactly once at startup with adapters registered in one place, so that algorithm count between the two registries can never drift apart.
3. As a maintainer, I want `TaskCenter` to resolve algorithm descriptors (name, resourceProfile) from the same registry the Agent uses, so that descriptor queries are consistent regardless of the caller.
4. As a maintainer reading `python_plugin_host`, I want algorithm execution to go through the same adapter interface the Agent uses, so that Python plugin algorithms and Agent-dispatched algorithms share one code path.
5. As a maintainer, I want dead modules removed from the build, so that I don't waste time reading, maintaining, or wondering about code that nothing calls.
6. As a contributor adding a new algorithm provider, I want one registration point with one adapter interface, so that I don't have to implement both `TaskAlgorithmAdapter` and `AtomicAlgorithmAdapter` for the same algorithm.
7. As a test author, I want algorithm registration tests to cover one registry, so that test setup doesn't need to populate two registries and assert consistency between them.
8. As a CI pipeline, I want fewer compiled translation units, so that incremental build times stay low.
9. As the Agent subsystem, I want `AlgorithmEngine::initialize()` to register provider adapters directly into `AtomicAlgorithmRegistry` without a mirroring step, so that the startup path is simpler and the ADR 0012 mirror hack is eliminated.
10. As a maintainer, I want the four different things named `ProgressCallback` to not include a dead virtual base class, so that name collisions are reduced by one.

## Implementation Decisions

### Registry Consolidation

- **`AlgorithmEngine` becomes a startup-only Provider discovery host.** It retains `initialize()`, `registerProvider()`, `registeredProviders()`, and the Provider adapter seam (`AlgorithmProviderAdapter`). It loses `findAlgorithm()`, `executeAlgorithm()`, `validateParameters()`, `registerAlgorithm()`, `registerProcessingAlgorithm()`, `populateFromProcessingRegistry()`, and `clear()`. The internal `m_adapters` map is removed.
- **`AlgorithmProviderAdapter::discoverAlgorithms` changes signature** from `discoverAlgorithms(AlgorithmEngine&)` to `discoverAlgorithms(AtomicAlgorithmRegistry&)` (or receives a registration callback). Each provider registers its adapters directly into the `AtomicAlgorithmRegistry` singleton, producing `AtomicAlgorithmAdapter` instances — not `TaskAlgorithmAdapter`.
- **`QgsProcessingProviderAdapter` and `PythonProcessingProviderAdapter`** produce `ProviderAlgorithmAdapter` (which already implements `AtomicAlgorithmAdapter`) instead of `QgsProcessingAlgorithmAdapter` (which implements `TaskAlgorithmAdapter`). The intermediate `QgsProcessingAlgorithmAdapter` class is deleted.
- **`TaskAlgorithmAdapter` interface is deleted.** Its only remaining concrete subclasses — `QgsProcessingAlgorithmAdapter` and `PythonAlgorithmAdapter` — are replaced by their `AtomicAlgorithmAdapter` equivalents.
- **`PythonAlgorithmAdapter`** is rewritten to implement `AtomicAlgorithmAdapter` (Json::Value in/out) instead of `TaskAlgorithmAdapter` (QVariantMap in/out). The `json_params_converter.h` utilities handle the type bridge internally.
- **`TaskCenter` migrates descriptor reads** from `AlgorithmEngine::findAlgorithm()` to `AtomicAlgorithmRegistry::findAdapter()`. The two call sites (resolveResourceProfile, enqueueTask name resolution) switch to reading `AlgorithmDescriptor` from the `AtomicAlgorithmAdapter`.
- **`AlgorithmDescriptor` has two definitions today** — one in `algorithm_engine.h` (sicnu namespace, Qt types) and one in `algorithm_descriptor.h` (sicnu::processing namespace, std/Json types). The `algorithm_engine.h` definition is deleted; `TaskCenter` uses the `sicnu::processing::AlgorithmDescriptor`.
- **`python_plugin_host::executeAlgorithm`** migrates from `AlgorithmEngine::instance().executeAlgorithm()` to `AtomicAlgorithmRegistry::instance().findAdapter()->execute()`.
- **`app_interface_bridge`** Python algorithm registration migrates from `AlgorithmEngine::registerAlgorithm(PythonAlgorithmAdapter)` to `AtomicAlgorithmRegistry::registerAdapter(PythonAtomicAdapter)`.
- **The ADR 0012 mirror callback** (`setProviderAlgorithmProvider`) is deleted — providers register directly, no mirror needed.

### Dead Code Deletion

- **`ErrorReporter`** (`error_reporter.h`, `error_reporter.cpp`): deleted. Zero production callers. Tests referencing it are deleted.
- **`ProcessingCache`** (`processing_cache.h`, `processing_cache.cpp`): deleted. Zero production callers. `test_processing_framework.cpp` ProcessingCache tests are deleted.
- **`ProgressCallback` virtual base + `SimpleProgressCallback`** (`progress_callback.h`, `progress_callback.cpp`): deleted. Zero production callers. The `std::function` typedefs with the same name in other files are unaffected.
- **CMakeLists.txt** in `src/processing/` is updated to remove the deleted source files.

## Testing Decisions

- **Test through the `AtomicAlgorithmRegistry` seam.** All algorithm registration, lookup, and execution tests exercise `AtomicAlgorithmRegistry::findAdapter()` and `AtomicAlgorithmAdapter::execute()`. This matches the existing test pattern in `test_tool_call_dispatcher.cpp`, `test_mcp_server.cpp`, and `test_pipeline_runner.cpp`.
- **Provider discovery is tested by verifying adapter count** in `AtomicAlgorithmRegistry` after `AlgorithmEngine::initialize()`. The existing `test_toolbox_coverage.cpp` pattern (assert GDAL/OTB/QGIS algorithm counts) serves as prior art.
- **`TaskCenter` descriptor resolution tests** verify that `enqueueTask` resolves the algorithm name and resource profile from `AtomicAlgorithmRegistry`. Prior art: existing `test_task_center.cpp` and `test_ui_task_center_contract.cpp`.
- **`PythonAlgorithmAdapter` tests** verify Json-in/Json-out execution through the `AtomicAlgorithmAdapter` interface. Prior art: `test_plugin_host.cpp`.
- **Dead module tests are deleted**, not migrated. Good tests test external behavior through the highest seam; these modules had no external behavior.

## Out of Scope

- **Unifying the GUI dialog execution path** (`async_algorithm_runner`, `contrast_stretch_dialog`) onto `submitJob`. This was identified as a separate improvement that would allow `markTaskRunning/Completed/Failed` to become private, but it is a distinct effort from registry consolidation.
- **Renaming the remaining `ProgressCallback` `std::function` typedefs** across `AtomicAlgorithmAdapter`, `cli::RsPipelineRunner`, and `GuiJobHandle`. The name collision is reduced by deleting the virtual base class; further renaming is cosmetic.
- **Refactoring `AlgorithmEngine::initialize()` provider wiring** beyond what's needed for direct registration. The provider discovery logic is startup-only and works; deeper restructuring is not justified by the current evidence.
- **`processing/algorithms/` pure computation modules** (image_enhancement, terrain_analysis, band_math, etc.). These are leaf libraries with no framework coupling and were not flagged by the design analysis.

## Further Notes

- The `AlgorithmDescriptor` in `sicnu::processing` (algorithm_descriptor.h) already carries `AgentMetadata`, `PortDescriptor` with typed inputs/outputs, and `toToolCallDefinition()`. The `sicnu::AlgorithmDescriptor` in `algorithm_engine.h` is a flat struct with only `id/name/group/description/parameterSchema/resourceProfile`. Consolidating onto the richer descriptor is a net gain — `TaskCenter` gets typed port information for free.
- `ProviderResourceProfile` is currently defined in `algorithm_provider_adapter.h` and used by both registries. This stays where it is — it's the Provider seam's concern, not the registry's.

# Ticket TICKET-01: AlgorithmProviderAdapter Interface Seam & Auto-Registration

- **Type**: `grilling`
- **Status**: Closed
- **Parent Map**: [MAP_algorithm_engine_modularization.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_algorithm_engine_modularization.md)

## Question

How should heterogeneous algorithm providers (`GdalToolsProvider`, `OtbToolsProvider`, `QgisAlgorithmsProvider`, `GenericCliProvider`, `PythonProcessingProvider`) register their descriptors and execution adapters with `AlgorithmEngine` and `AtomicAlgorithmRegistry`?

## Resolution

Adopt **Option A**: Define a uniform C++ `AlgorithmProviderAdapter` interface seam with self-contained `initialize()` and `discoverAlgorithms()` hooks.

### Key Architectural Details
- **Seam**: `AlgorithmEngine` manages provider life cycles via `std::vector<std::unique_ptr<AlgorithmProviderAdapter>> m_providers`.
- **Registration**: Each provider populates its `AtomicAlgorithmAdapterPtr` instances into `AtomicAlgorithmRegistry` during application startup or on-demand plugin loading.
- **Locality & Leverage**: Hides provider loading internals behind a single seam; LLM Agent tool call exporters (`exportOpenAiToolDefinitions`) query `AtomicAlgorithmRegistry` directly.

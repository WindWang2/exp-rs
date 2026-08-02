# Ticket TICKET-14: Headless CLI & Unit Test Suite Verification

- **Type**: `task`
- **Status**: Closed
- **Parent Map**: [MAP_plugin_host_unification.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_plugin_host_unification.md)

## Question

How will headless execution in `sicnu_geo_rs_cli` and unit tests verify clean C++ and Python plugin loading through `PluginHost` without Qt GUI widget instantiation?

## Resolution

### 1. Headless CLI Integration (`src/cli/rs_pipeline_runner.cpp`)
Update `sicnu_geo_rs_cli` to instantiate `PluginHost` directly, passing a headless `SicnuAppInterface` backed by `DataManager`. Verify that plugins register algorithms into `AtomicAlgorithmRegistry` headlessly.

### 2. Unit Test Suite (`tests/test_plugin_host.cpp`)
Create a dedicated CTest executable `test_plugin_host` that:
1. Instantiates `PluginHost` with a headless `SicnuAppInterface` (no `QgsMapCanvas`, no `QgsLayerTreeView`, zero QWidget instances).
2. Loads a mock C++ plugin (`SicnuPluginInterface`) and a sample Python plugin (`metadata.txt` + `__init__.py`).
3. Verifies `isPluginLoaded("sample_plugin") == true`.
4. Executes a plugin command over IPC and asserts return value.
5. Calls `unloadAll()` and verifies clean process pool shutdown.

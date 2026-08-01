# Unify Plugin Host Facade Specification

## Problem Statement

Previously, plugin management in the application was split between `PluginManager` and `PluginHost`. `PluginManager` served as a shallow 1:1 pass-through wrapper around `PluginHost`. Every plugin lifecycle method (`loadPlugins`, `loadPlugin`, `loadPythonPlugin`, `unloadAll`, `loadedPlugins`, `plugin`, `isPluginLoaded`) and signal (`pluginLoaded`, `pluginUnloaded`, `pluginError`) was forwarded 1:1 without adding any behavior. This created double indirection, bloated the C++ core library surface area, and forced developers and maintainers to bounce between shallow wrappers when navigating plugin lifecycle handling.

## Solution

Collapse `PluginManager` into `PluginHost`, establishing `PluginHost` as the single canonical deep module for C++ and Python plugin lifecycle management across desktop GUI shells, headless CLI runners, and unit test suites.

## User Stories

1. As a desktop GIS user, I want C++ and Python plugins to be discovered and loaded seamlessly upon application launch, so that plugin actions and UI panels are available without unnecessary wrapper layers.
2. As a headless CLI runner user, I want Python processing plugins to load headlessly through `PluginHost`, so that script execution does not instantiate desktop GUI widgets.
3. As a plugin developer, I want a single clear `PluginHost` interface seam for querying loaded plugins and listening to load/error events, so that I don't have to navigate redundant proxy wrappers.
4. As an AI Agent copilot, I want plugin tools and algorithms registered through a unified `PluginHost` facade, so that tool schemas are consistently exposed without missing wrapper method delegations.
5. As a core C++ maintainer, I want plugin lifecycle, worker process pool management, and QObject signals to concentrate in one deep module (`PluginHost`), so that bug fixes and lifecycle updates only happen in one place.
6. As a test suite author, I want to instantiate and test `PluginHost` headlessly in Catch2 unit tests, so that test assertions target the actual lifecycle owner without double indirection.

## Implementation Decisions

1. **Single Deep Seam**: Collapse `PluginManager` pass-through methods and signals into `PluginHost`. `PluginHost` is the sole `QObject` owner for both C++ (`QPluginLoader`) and Python (`PythonPluginHost`) plugin lifecycles.
2. **Desktop Shell Integration**: Update the desktop application shell (`QgisDesktopWindow` / `SicnuMainWindow`) to hold a direct `std::unique_ptr<PluginHost>` (or raw pointer for Qt parent-child ownership in legacy shells), invoking `setAppInterface()` and `loadPlugins()` directly.
3. **Domain Model Canonicalization**: Update `CONTEXT.md` to define **Plugin Host** (`PluginHost`) as the canonical domain concept and class name, deprecating the term "PluginManager".
4. **ADR Recording**: Record the architectural decision as `ADR 0024: Unify Plugin Host Facade Architecture` (`docs/adr/0024-unify-plugin-host-facade.md`) to document the removal of double indirection and preserve seam discipline.

## Testing Decisions

1. **Behavioral Test Surface**: Unit tests test external behavior (plugin discovery, loading success/failure, signal emission, and plugin instance retrieval) through `PluginHost`'s public interface, without asserting internal pass-through delegation.
2. **Headless Execution**: `PluginHost` tests run headlessly using Catch2 with an injected `SicnuAppInterface` and `ProjectContext`, verifying GUI-free plugin discovery and worker process execution.
3. **Prior Art**: Extends existing Catch2 test patterns in `tests/test_plugin_host.cpp` and `tests/test_python_plugin_host.cpp`.

## Out of Scope

1. Redesigning Python plugin IPC JSON-RPC protocol messages (`AppInterfaceBridge` / `PythonIpcServer`).
2. Modifying QGIS C++ plugin interface declarations (`SicnuPluginInterface`).
3. Adding new plugin installation or marketplace UI dialogs.

## Further Notes

- Aligns with ADR 0014 (Out-of-Process Python Plugin Host) and ADR 0015 (ActiveViewHost Deepening).
- Architectural terms strictly follow the `/codebase-design` vocabulary: **Module** (`PluginHost`), **Interface** (`loadPlugins`, `loadedPlugins`, `plugin`), **Depth** (deep module), **Locality** (concentrates lifecycle in one module), and **Leverage** (one interface for GUI, CLI, and tests).

# 0024 Unify Plugin Host Facade Architecture

We decided to collapse the shallow `PluginManager` pass-through wrapper into `PluginHost` as the single canonical plugin lifecycle module across desktop shell and headless runner surfaces.

### Context & Decision
Previously, `PluginManager` existed in `src/core/plugin_manager.h` as a 100% shallow forwarding wrapper around `PluginHost`. Every method call (`loadPlugins`, `loadPlugin`, `loadPythonPlugin`, `unloadAll`, `loadedPlugins`, `plugin`, `isPluginLoaded`) and signal (`pluginLoaded`, `pluginUnloaded`, `pluginError`) was forwarded 1:1 with zero added behavior, creating double indirection and forcing callers to bounce between shallow wrappers.

1. **Single Deep Plugin Seam**: Eliminate `PluginManager` and collapse plugin lifecycle management into `PluginHost`. Desktop shell (`MainWindow`), headless CLI runner, and test suites interact exclusively with `PluginHost`.
2. **Canonical Domain Vocabulary**: `CONTEXT.md` defines **Plugin Host** (`PluginHost`) as the sole lifecycle owner for C++ (`QPluginLoader`) and Python (`PythonPluginHost`) plugins.
3. **Locality & Leverage**: Plugin discovery, loading, signal emission, and plugin instance queries concentrate in `PluginHost`. One interface pays back across desktop GUI and headless surfaces without pass-through indirection.

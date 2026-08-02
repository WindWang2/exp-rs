# Ticket TICKET-10: Scope & Destination Statement

- **Type**: `grilling`
- **Status**: Closed
- **Parent Map**: [MAP_plugin_host_unification.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_plugin_host_unification.md)

## Question

What is the precise destination, architectural boundary, and success criteria of the PluginHost Unification effort?

## Resolution

The destination is a GUI-free unified `PluginHost` module in `src/core/plugin_host.h` that:
1. **Unifies C++ & Python Plugin Lifecycles**: Owns discovery, loading, dependency resolution, execution adapter creation, and unloading for both C++ dynamic libraries (`.so`/`.dll` via `QPluginLoader`) and out-of-process Python plugins (`metadata.txt` + `__init__.py` via `PythonPluginHost`).
2. **Removes Widget Dependencies**: Replaces `initialize(QgsMapCanvas*, QgsLayerTreeView*)` in `SicnuPluginInterface` with `initialize(SicnuAppInterface *iface)`.
3. **Supports Dual Hosting Modes**: Headless surfaces (CLI runner `sicnu_geo_rs_cli`, unit tests, MCP server) consume `PluginHost` directly without GUI widget dependencies. Desktop GUI (`QgisDesktopWindow`) consumes `PluginManager` as a thin UI adapter decorating `PluginHost` with main window menu (`QMenu`) and action (`QAction`) injections.

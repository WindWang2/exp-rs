# Wayfinder Map: Unify Plugin Lifecycle in Headless PluginHost

## Destination

A single GUI-free `PluginHost` module in `src/core/plugin_host.h` that owns the complete lifecycle (discovery, loading, dependency resolution, execution adapter creation, and unloading) of both C++ dynamic libraries (`QPluginLoader`) and out-of-process Python plugins (`PythonPluginHost`), eliminating all `QgsMapCanvas*` and `QgsLayerTreeView*` widget dependencies from core plugin management so both desktop GUI (`QgisDesktopWindow`) and headless CLI/MCP surfaces consume the same host seam.

## Notes

- **Domain Glossary**: Consult [CONTEXT.md](file:///home/kevin/projects/exp-rs/CONTEXT.md) for Plugin Host, Python Plugin Host, Application Interface Facade (`iface`), Headless Asset Seam (`AppInterfaceBridge`), Data Manager, and Active View Host terms.
- **Architecture Vocabulary**: Apply `/codebase-design` deep module terms (**module**, **interface**, **depth**, **seam**, **adapter**, **leverage**, **locality**).
- **Relevant ADRs**: ADR 0014 (Out-of-Process Python Host Architecture), ADR 0015 (ActiveViewHost Deepening), ADR 0023 (GUI-free Python Plugin Host).

## Decisions so far

- [TICKET-10: Scope & Destination Statement](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_10_plugin_host_destination.md) — Locked destination as GUI-free unified `PluginHost` for C++ and Python plugins.
- [TICKET-11: C++ Plugin Interface Context Seam](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_11_plugin_interface_context_seam.md) — Refactor `SicnuPluginInterface::initialize(SicnuAppInterface *iface)` to eliminate `QgsMapCanvas*` / `QgsLayerTreeView*`.
- [TICKET-12: Headless PluginHost Core Module](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_12_headless_plugin_host.md) — Implement `PluginHost` in `src/core/plugin_host.h` with C++ and Python plugin auto-discovery.
- [TICKET-13: Desktop PluginManager UI Adapter](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_13_desktop_plugin_manager_adapter.md) — Refactor `PluginManager` in `src/app` into a thin UI menu and action adapter over `PluginHost`.
- [TICKET-14: Headless CLI & Unit Test Suite Verification](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_14_headless_cli_and_tests_verification.md) — Wire `PluginHost` into CLI runner and verify C++/Python plugin loading headlessly in unit tests.

## Not yet specified

- Dynamic plugin dependency sorting (topological load order based on manifest dependencies).
- Hot-reloading and file watcher hooks for development iterations of out-of-process Python plugins.
- Automated plugin algorithm catalog schema export for LLM tool discovery.

## Out of scope

- UI dialog widget (`QgsPluginManagerBase` settings dialog).
- In-process CPython execution (CPython remains out-of-process in worker daemons per ADR 0014).

# ADR 0035: Collapse PythonAppInterfaceProxy into AppInterfaceBridge

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`PythonAppInterfaceProxy` was a 183-line shallow middleman class that wrapped `AppInterfaceBridge` and delegated all IPC method dispatches to `AppInterfaceBridge::dispatchIpcMessage`. Its only distinct logic was managing `QMenu *parentMenu` and creating `QAction` items for `ui.add_plugin_menu` requests.

## Decision

1. **Absorb Proxy Seam**: Deepen `AppInterfaceBridge` to accept an optional `QMenu *parentMenu`, absorb `bindIpcServer`, and handle `ui.add_plugin_menu` action creation directly.
2. **Direct Ownership**: `PythonPluginAdapter` owns `AppInterfaceBridge` directly (`m_bridge`), binding the IPC server on worker acquisition.
3. **Delete Shallow Class**: Delete `python_app_interface_proxy.h` and `python_app_interface_proxy.cpp`.

## Consequences

- **Simplification**: One shallow wrapper class removed from the out-of-process Python plugin subsystem.
- **Locality**: JSON-RPC IPC method decoding and UI action registration are concentrated in `AppInterfaceBridge`.
- **Testability**: `AppInterfaceBridge` can be tested directly with or without a GUI parent menu.

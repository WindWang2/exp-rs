# 0025 Consolidate AppInterfaceBridge IPC Seam Architecture

We decided to deepen `AppInterfaceBridge` to encapsulate JSON-RPC request decoding and response payload generation, turning `PythonAppInterfaceProxy` into a thin desktop Qt UI menu adapter.

### Context & Decision
Previously, JSON-RPC IPC request handling was implemented inside a 200-line `if/else if` chain in `PythonAppInterfaceProxy::handleIpcMessage`. `AppInterfaceBridge` only provided helper methods returning summary structs (`ActiveLayerSummary`, `CanvasViewportSummary`). This split protocol serialization across proxy, server, and bridge layers, preventing headlessly testing IPC message handling without constructing socket servers or Qt UI elements.

1. **Deep IPC Seam**: Deepen `AppInterfaceBridge` to expose `bool dispatchIpcMessage( const QJsonObject &message, QJsonObject &response )`. It handles parameter validation, catalog/view queries, algorithm registration, and response JSON generation for all headless IPC methods (`catalog.*`, `data.*`, `canvas.*`, `ui.push_message_bar`, `processing.register_algorithm`).
2. **Thin Desktop View Adapter**: `PythonAppInterfaceProxy` delegates incoming IPC messages directly to `m_bridge.dispatchIpcMessage(message, response)`. It handles only desktop `QAction`/`QMenu` lifetime management for `ui.add_plugin_menu`.
3. **Headless Locality & Testability**: IPC request and response formats concentrate inside `AppInterfaceBridge`. Catch2 unit tests exercise `dispatchIpcMessage` headlessly with zero socket I/O or `QWidget` dependencies.

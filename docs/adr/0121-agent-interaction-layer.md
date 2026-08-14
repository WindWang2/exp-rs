# ADR 0121 — Agent Interaction Layer

- Status: Accepted (2026-08-14)
- Scope: `src/agent`, `src/processing/framework`, `src/app/display`

## Context

Prior to this architecture, AI agents in `exp-rs` (via Agent Copilot or MCP protocol) could execute remote sensing algorithms asynchronously through `AtomicAlgorithmRegistry` and `TaskCenter`, but could not control or query the GIS software state interactively. The only existing canvas interaction was a hardcoded `canvas:draw_roi` handler tied directly to the `AgentCopilotDockWidget` GUI class.

This caused several limitations:
1. External MCP agents could not inspect viewport state (CRS, spatial extent, zoom scale, rotation, active layer).
2. Agents could not navigate or reframe the map canvas (e.g. `set_extent`, `zoom_to_layer`, `zoom_to_asset`, `fit_all`).
3. View control logic was tightly coupled with Qt widget implementation details (`AgentCopilotDockWidget`), preventing headless testing and multi-client access.
4. ToolCallDispatcher lacked an open, extensible registry for interaction actions, relying on single-purpose callback hooks.

## Decision

We establish a dedicated Agent Interaction Layer bridging Agents and the QGIS Display subsystem:

```
Agent (Copilot / MCP Client)
      │
      ▼
InteractionToolRegistry (view:*, roi:*, canvas:*, layer:*)
      │
      ▼
ViewControlService (GUI-safe decoupled facade)
      │
      ▼
QgisDisplayManager / QgsMapCanvas / QgsRubberBand
```

### 1. ViewControlService (`src/agent/view_control_service.h`, `.cpp`)
- **Decoupled Service**: Designed as an independent, headless-testable service independent of `AgentCopilotDockWidget`.
- **Comprehensive API**:
  - `getState()`: Returns view ID, destination CRS authid, spatial extent (`xmin, ymin, xmax, ymax`), zoom scale, rotation, active layer, and current ROI geometry.
  - `setExtent(params)`: Sets canvas extent via `bbox` object, `extent` array `[xmin, ymin, xmax, ymax]`, or `geometry` WKT, with automatic coordinate reprojection to canvas CRS if `crs` is specified.
  - `zoomToLayer(params)`: Locates layer by ID or name across `QgisDisplayManager`, `QgsMapCanvas`, and `QgsProject`, reprojects extent, and updates canvas.
  - `zoomToAsset(params)`: Looks up DataManager asset by UUID and resolves to display layer.
  - `fitAll()`: Fits canvas extent to all visible layers.
  - `setScale(params)`: Sets canvas zoom scale.
  - `setRoi(params)` / `clearRoi()`: Manages `QgsRubberBand` on the canvas scene, handles WKT / bbox parsing with CRS transformation, and caches ROI WKT.
- **Robust Validation**: Comprehensive parameter checking rejecting non-numeric coordinates, inverted boundaries (`xmin >= xmax`, `ymin >= ymax`), non-positive scales, and invalid CRS strings with structured JSON errors.

### 2. Thread Safety Model
- `QgsMapCanvas`, `QgsRubberBand`, and `QGraphicsScene` live exclusively on the Qt GUI main thread.
- `ViewControlService` guarantees 100% thread safety via `executeOnGuiThread(Func&&)`:
  - If called on the GUI thread: Executes immediately inline.
  - If called from a worker/background thread: Marshals execution via `QMetaObject::invokeMethod(this, ..., Qt::BlockingQueuedConnection)`, blocking safely until the GUI thread completes the action.

### 3. InteractionToolRegistry (`src/agent/interaction_tool_registry.h`, `.cpp`)
- Mirrors `AtomicAlgorithmRegistry` architecture for interactive GIS tools.
- Supports discovery (`listTools()`, `findTool()`), execution (`execute(name, arguments)`), schema validation, and OpenAI function calling export (`exportOpenAiToolDefinitions()`, `exportSystemPromptCatalog()`).
- Namespace conventions:
  - `view:*`: Viewport inspection and navigation (`view:get_state`, `view:set_extent`, `view:zoom_to_layer`, `view:zoom_to_asset`, `view:fit_all`, `view:set_scale`).
  - `roi:*`: Region-of-interest management (`roi:set`, `roi:clear`).
  - `canvas:*`: Backward-compatible canvas actions (`canvas:draw_roi`).
  - `layer:*`: Reserved for future layer manipulation.

### 4. ToolCallDispatcher Integration (`src/processing/framework/tool_call_dispatcher.h`, `.cpp`)
- `isInteractionAction(name)` recognizes `view:`, `roi:`, `canvas:`, and `layer:` namespaces.
- `setInteractionActionHandler()` allows in-process synchronous dispatch.
- When an interaction tool is called via `ToolCallDispatcher::submit()` or `dispatchAndAwait()`:
  - Validates envelope and skips asynchronous TaskCenter scheduling.
  - Dispatches directly to `InteractionActionHandler` returning structured result JSON synchronously.
  - Preserves 100% backward compatibility for algorithm tools (`rs:*`, `gdal:*`, `otb:*`) and legacy `canvas:` handlers.

### 5. MCP Server Extensions (`src/agent/mcp_server.h`, `.cpp`)
- Registered new meta-tools in MCP:
  - `list_interaction_tools`: Lists all interactive GIS tools with descriptions and JSON input schemas.
  - `get_interaction_schema`: Returns full input schema for a specific interaction tool.
- Whitelisted `view:`, `roi:`, `canvas:`, `layer:` prefixes in `idHasAllowedPrefix()`.
- MCP direct invocation routes interaction tools synchronously through `ToolCallDispatcher` / `InteractionToolRegistry`.

## Relationship to AtomicAlgorithmRegistry

| Feature | AtomicAlgorithmRegistry | InteractionToolRegistry |
| :--- | :--- | :--- |
| **Domain** | Remote sensing algorithms & processing operators | GIS UI state, viewport, canvas ROI, layer display |
| **Namespaces** | `rs:*`, `gdal:*`, `otb:*`, `qgis:*`, `opencv:*` | `view:*`, `roi:*`, `canvas:*`, `layer:*` |
| **Execution** | Asynchronous via `TaskCenter` (jobs, threads, processes) | Synchronous on Qt GUI main thread |
| **Output** | Data Manager assets, raster/vector files | Viewport JSON state, visual canvas feedback |

## Future Extensions

1. **Raster Value Query**: Inspect pixel values, band profiles, and spectral curves at coordinate `(x, y)` or within an ROI.
2. **Layer Styling & Rendering**: Toggle band combinations (RGB / False Color / Singleband pseudocolor), adjust stretch ranges, and opacity.
3. **Interactive Measurement**: Distance, area, and elevation profile tools callable by agents.
4. **Layer Tree Mutation**: Reorder layers, create groups, toggle visibility, and adjust blending modes.

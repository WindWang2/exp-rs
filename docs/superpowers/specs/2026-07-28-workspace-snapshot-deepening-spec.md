# WorkspaceSnapshot Deepening & Agent Context Resolver Specification

## Problem Statement

The AI Agent Copilot relies on `AgentContextResolver` to generate System Prompt context headers from the current GIS shell state. Previously, `AgentContextResolver` directly inspected multiple low-level UI objects (`DataManager*`, `ActiveViewHost*`, and `QgsMapCanvas*`) during prompt formatting. This shallow design mixed context gathering with string formatting, making it impossible to test prompt construction without launching live Qt/QGIS GUI widgets and introducing potential crashes if UI objects were mutated or destroyed mid-formatting.

## Solution

Deepen `AgentContextResolver` by introducing an immutable, strongly typed `WorkspaceSnapshot` C++ value object. Context gathering is isolated into a single-pass `WorkspaceSnapshot::capture()` call that queries `DataManager` and `ActiveViewHost` facades (with zero `QgsMapCanvas*` dependencies). Prompt string formatting and JSON serialization are implemented directly on `WorkspaceSnapshot`, providing a clean seam for fast, offline, GUI-free Catch2 unit testing.

## User Stories

1. As a GIS analyst interacting with the AI Agent Copilot, I want the LLM to receive accurate, up-to-date workspace context (active layers, CRS, canvas extent, scale) so that the agent generates relevant algorithm execution plans.
2. As a software engineer maintaining `exp-rs`, I want `AgentContextResolver` to depend on an immutable `WorkspaceSnapshot` value object so that context formatting is decoupled from live Qt/QGIS widget lifetimes.
3. As a developer writing automated tests, I want to construct mock `WorkspaceSnapshot` instances in pure C++ without initializing `QApplication` or map canvas windows so that tests run instantly and deterministically.

## Implementation Decisions

- **Strongly Typed Snapshot Structs**: Define `DataAssetInfo`, `MapViewSnapshot`, and `WorkspaceSnapshot` C++ value structs in the `sicnu::agent` namespace.
- **Facade-Only Context Capture**: `WorkspaceSnapshot::capture()` queries `DataManager` for asset metadata and `ActiveViewHost` for map extent, scale, CRS, and selected layer name. No raw `QgsMapCanvas*` pointers or internal QGIS canvas settings are accessed directly.
- **Clean Prompt Formatting Seam**: Move JSON serialization (`.toJson()`) and System Prompt text rendering (`.toSystemPromptHeader()`) to `WorkspaceSnapshot` and delegate `AgentContextResolver::formatSystemContextPrompt` directly to it.
- **ADR 0013 & ADR 0015 Alignment**: Preserves the AI Agent system prompt structure specified in ADR 0013 while enforcing the single-pointer facade principle defined in ADR 0015.

## Testing Decisions

- **Behavioral Testing Seam**: Test external behavior via Catch2 unit tests in `tests/test_workspace_snapshot.cpp`.
- **Pure C++ Offline Tests**: Construct `WorkspaceSnapshot` instances with mock asset and map view values; assert correct JSON key formatting and System Prompt string generation without `QApplication`.
- **Capture Integration Tests**: Pass stubbed `DataManager` and `ActiveViewHost` objects into `WorkspaceSnapshot::capture()` and verify field extraction accuracy.

## Out of Scope

- Modifying the SSE streaming client (`LlmStreamingClient`) or OpenAI tool call parsing logic.
- Adding real-time websocket notifications for workspace snapshot mutations.

## Further Notes

- Recorded in `CONTEXT.md` under Ubiquitous Language as **Workspace Snapshot**.

# AI Agent Copilot UI & Streaming LLM Client Feature Specification

## Problem Statement

Remote sensing scientists and GIS analysts currently have to manually search through algorithm toolboxes, inspect complex parameter forms, and manually chain multiple raster operations together. While SICNU GEO RS now features an atomic algorithm registry and workflow engine (ADR 0011/0012), users lack an intelligent, conversational natural language copilot that can perceive active workspace data assets, translate natural language instructions into tool execution requests or multi-step DAG plans, and automatically render and execute them directly inside the application shell.

## Solution

Build an integrated **AI Agent Natural Language Copilot UI (`AgentCopilotDockWidget`)** and native C++ **Streaming LLM Client (`LlmStreamingClient`)** (ADR 0013):
1. **Right-Dockable Chat Panel**: A dedicated `QDockWidget` featuring a streaming message history view, DeepSeek-R1 reasoning chain cards (`<think>`), tool call progress widgets, and interactive DAG plan approval cards.
2. **Native Streaming LLM Client**: Asynchronous C++ HTTP SSE client built on `QNetworkAccessManager` supporting DeepSeek, Qwen, Ollama, vLLM, and OpenAI API endpoints with automatic tool schema injection from `AtomicAlgorithmRegistry`.
3. **Workspace Context Resolver**: `AgentContextResolver` dynamically inspects `DataManager` (active raster/vector assets, file paths, CRS, band references) and `ActiveViewHost` (selected layer, canvas extent) to inject structured workspace context snapshots into LLM System Prompts.
4. **Visual Pipeline Graph Synchronization**: Multi-step DAG agent plans automatically instantiate as visual `WorkflowDefinition` node graphs on `PipelineCanvasWidget` for interactive preview, parameter tuning, and execution via `AgentWorkflowExecutor::executeAgentPlan`.
5. **Config & Profile Persistence**: `LlmConfigManager` manages API Key, Base URL, Model Name, and Temperature settings via `QSettings` with pre-configured provider templates and a ⚙️ **Model Settings** dialog.

## User Stories

1. As a remote sensing analyst, I want a dedicated AI Agent Copilot dock panel on the right side of the main window, so that I can interact with an AI assistant via natural language without leaving my primary GIS workspace.
2. As a user, I want the AI Agent to automatically know which raster and vector datasets are currently loaded in my workspace, so that I don't have to manually type out long file paths or Asset IDs.
3. As a user, I want the AI Agent to stream its response in real time, so that I don't have to wait for the entire LLM response to complete before reading the initial output.
4. As a user working with reasoning models like DeepSeek-R1, I want to see the model's thinking/reasoning chain in a collapsible card, so that I can understand how the agent arrived at its decision.
5. As an analyst, I want single-step algorithm recommendations to display a tool execution card with parameter details, so that I can verify the arguments before running the algorithm.
6. As an analyst, I want multi-step agent plans to automatically render onto the visual DAG Pipeline Canvas, so that I can inspect the generated workflow graph, edit parameters, and click "Run Pipeline".
7. As a user, I want tool executions to report real-time progress percentage and status indicators (⚪ Idle, 🔵 Running, 🟢 Success, 🔴 Failure) inside the chat history stream, so that I can monitor execution without switching tabs.
8. As a user, I want to easily switch between LLM providers (DeepSeek, Qwen, Ollama local, OpenAI) in a settings dialog, so that I can use different cloud or local models depending on data privacy requirements.
9. As a developer, I want all LLM network communications to use native C++ Qt networking (`QNetworkAccessManager`), so that the system remains lightweight without requiring Python dependencies for LLM communication.
10. As a user, I want intermediate algorithm outputs to automatically register as `TaskTemporary` Data Assets in `DataManager`, so that downstream agent steps can reference them as inputs (`$step1.output`).

## Implementation Decisions

- **Architecture ADR**: Governed by **ADR 0013: AI Agent Copilot UI & Streaming LLM Client Architecture** recorded in `CONTEXT.md`.
- **Dock Panel UI**: `AgentCopilotDockWidget` inherits from `QDockWidget` and is registered with `MainWindow` docking system. Contains a custom scrollable message list (`AgentMessageListView`), a model selector dropdown, a settings ⚙️ button, and a multi-line prompt input field (`AgentPromptInputWidget`).
- **Streaming HTTP Engine**: `LlmStreamingClient` subclassing `QObject` wraps `QNetworkAccessManager` and `QNetworkReply`. Parses Server-Sent Events (`data: { ... }`) line-by-line in real time. Emits `reasoningTokenReceived(QString)`, `contentTokenReceived(QString)`, `toolCallParsed(QJsonObject)`, `finished()`, and `errorOccurred(QString)` signals.
- **Context Injection Engine**: `AgentContextResolver` queries `DataManager` and `ActiveViewHost` before each prompt submission, serializing active assets into a concise JSON payload injected into the `system` prompt context.
- **Tool Schema Auto-Export**: `LlmStreamingClient` automatically fetches all registered algorithms from `AtomicAlgorithmRegistry::instance().exportOpenAiToolDefinitions()` and includes them in the `tools` field of `chat/completions` API requests.
- **Visual Canvas Synchronization**: `AgentCopilotDockWidget` connects to `PipelineEditorDock` and `PipelineCanvasWidget`. When a multi-step plan is generated by the LLM, `WorkflowDefinition::fromJson()` parses the plan and populates the visual node graph.
- **Settings & Profile Manager**: `LlmConfigManager` manages `LlmProviderProfile` instances persisted via `QSettings` (`[AI_Agent]` section in `sicnu_geo_rs.conf`). Includes built-in templates for DeepSeek (`api.deepseek.com`), Qwen (`dashscope.aliyuncs.com`), Ollama (`localhost:11434`), and custom OpenAI-compatible endpoints.

## Testing Decisions

- **Seam 1: `LlmStreamingClient` & `AgentContextResolver` Headless API Seam**
  - Tested in Catch2 test executable `test_llm_streaming_client.cpp`.
  - Uses mock HTTP SSE stream payloads to test real-time token parsing, DeepSeek-R1 reasoning chain extraction (`<think>`), tool call JSON payload extraction, and workspace context snapshot generation from `DataManager`. Zero Qt GUI dependencies.
- **Seam 2: `AgentCopilotDockWidget` & `LlmSettingsDialog` Shell UI Seam**
  - Tested in Catch2 test executable `test_agent_copilot_ui.cpp`.
  - Tests widget instantiation, chat message item rendering, tool call progress widget signal handling, plan approval card signal emission, and settings profile serialization/deserialization.

## Out of Scope

- Multi-modal image input streaming (sending raw raster pixel tiles directly into LLM vision context windows).
- Voice input / speech-to-text integration.

## Further Notes

- Complements ADR 0011 (Task Pipeline & Workflow Visual Editor UI) and ADR 0012 (Atomic Algorithm Adapter & Tool Calling Engine).
- All network operations use standard non-blocking Qt signals/slots.

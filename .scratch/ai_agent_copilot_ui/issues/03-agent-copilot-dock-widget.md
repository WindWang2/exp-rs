# 03 — Wave C: AgentCopilotDockWidget Right Panel & Streaming Chat History UI

**What to build:**
`AgentCopilotDockWidget` integrated into main window docking system with streaming message history view, collapsible DeepSeek-R1 reasoning cards, tool call progress cards, prompt input field, model selector dropdown, and ⚙️ **Model Settings** dialog (`LlmSettingsDialog`).

**Blocked by:** 02 — Wave B: AgentContextResolver Dynamic Workspace Context Injection

**Status:** ready-for-agent

## Acceptance Criteria
- [ ] Implement `AgentCopilotDockWidget` subclassing `QDockWidget` registered with `MainWindow`.
- [ ] Implement `AgentMessageListView` displaying real-time streaming Token responses, collapsible DeepSeek-R1 reasoning `<think>` cards, and tool call progress widgets.
- [ ] Implement `AgentPromptInputWidget` with prompt input text area, Send button, and Cancel/Stop button.
- [ ] Implement `LlmSettingsDialog` modal dialog for editing API Key, Base URL, Model Name, Temperature, and testing network connection.
- [ ] Add 🤖 **AI Copilot** button to main ribbon tab.
- [ ] Add Qt Catch2 UI tests in `tests/test_agent_copilot_ui.cpp` verifying widget creation, signal emissions, and settings dialog persistence.

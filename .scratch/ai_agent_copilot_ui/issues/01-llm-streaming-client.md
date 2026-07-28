# 01 — Wave A: LlmStreamingClient C++ SSE Client & LlmConfigManager Settings Engine

**What to build:**
Headless C++ asynchronous HTTP SSE streaming client (`LlmStreamingClient`) subclassing `QObject` wrapping `QNetworkAccessManager`. Parses real-time Token streams, DeepSeek-R1 `<think>` reasoning tokens, and OpenAI-compatible Tool Call JSON payloads. `LlmConfigManager` manages API Key, Base URL, Model Name, and Temperature profiles in `QSettings` with pre-packaged templates (DeepSeek, Qwen, Ollama, OpenAI). Auto-injects algorithm tool schemas from `AtomicAlgorithmRegistry`.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

## Acceptance Criteria
- [ ] Implement `LlmStreamingClient` subclassing `QObject` wrapping `QNetworkAccessManager` with non-blocking SSE line-by-line stream parsing.
- [ ] Parse DeepSeek-R1 `<think>` reasoning tokens and emit `reasoningTokenReceived(QString)` signal.
- [ ] Parse content tokens and emit `contentTokenReceived(QString)` signal.
- [ ] Parse OpenAI-compatible tool call function JSON deltas and emit `toolCallParsed(QJsonObject)` signal.
- [ ] Auto-inject algorithm tool definitions from `AtomicAlgorithmRegistry::instance().exportOpenAiToolDefinitions()`.
- [ ] Implement `LlmConfigManager` managing `LlmProviderProfile` instances persisted via `QSettings` with DeepSeek, Qwen, Ollama, and OpenAI templates.
- [ ] Add Headless Catch2 unit tests in `tests/test_llm_streaming_client.cpp` verifying mock SSE stream parsing and profile serialization.

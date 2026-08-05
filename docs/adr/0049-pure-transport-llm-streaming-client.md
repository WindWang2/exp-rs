# ADR 0049: Make LlmStreamingClient a True Pure Transport

## Status
Accepted

## Context
`LlmStreamingClient::sendChatCompletion( messages, enableTools )` reached
into the global `AtomicAlgorithmRegistry` to inject tool schemas whenever
`enableTools` was true — the caller (the Copilot dock, which owns execution
policy) could not choose which tools went on the wire. Request assembly
(endpoint normalization, auth header, body) was built inline with no test
seam, and the body honored `m_profile.stream` while the SSE parser only
understands `data:` lines — a `stream=false` response was silently dropped.

## Decision
1. **Caller-supplied tools**: `sendChatCompletion( messages, tools = {} )`.
   An empty tools array means no tool injection; the registry lookup moves
   to the Copilot dock, which converts the exported `Json::Value` with the
   shared `json_params_converter.h` helpers.

2. **Extract `buildChatRequest( profile, messages, tools )`**: a pure static
   returning the `QNetworkRequest` + JSON body pair. `sendChatCompletion`
   becomes build → post → SSE-parse; the builder is the new test seam.

3. **Always send `"stream": true`**: the transport is SSE-only, so the knob
   can no longer produce an unparseable request. `LlmProviderProfile.stream`
   stays for persistence/UI but is decoupled from the wire format.

4. **Delete the dead `agent_tool_call_exporter.h` include**.

## Consequences
- **Transport stays pure**: the wire carries exactly the tools the caller
  chooses; the global algorithm catalog is no longer a transport dependency.
- **Wire payload is unit-tested**: endpoint normalization, auth header, body
  shape, and tools presence are pinned by tests.
- **No more silent-drop mode**: every request is `stream: true`; the
  `stream=false` path that produced zero tokens is unreachable.
- **UI/config surface unchanged**: the settings dialog and QSettings schema
  keep the `stream` field (removal out of scope).

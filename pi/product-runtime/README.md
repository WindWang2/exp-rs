# Controlled product Pi session

Current owner: the in-product Qt sidecar (`PiProcess` /
`PiConversationRuntime`) plus `AgentToolHost`. This directory is the Node
wrapper only. See `docs/development/pi-product-agent/` for the current
architecture. The P0–P3 sections below remain the stage write-ups.

This module assembles the pinned, patched Pi 1.5.2 SDK using explicit model,
memory-only settings/credentials/session storage, an empty resource loader,
and only the host tool definitions supplied by the caller. It neither starts
`exp-rs --mcp` nor executes Python or filesystem tools. The Qt host already
owns tool approval, execution, cancellation acknowledgement and asset commit;
this wrapper must not grow a second executor.

`createControlledSession({nodeModules, cwd, profile, tools, onToolRequest,
systemPrompt})` requires absolute prepared paths. `profile` contains the explicit
base URL, model, in-memory API key, context window and maximum output tokens.
The native AS-015 transport rule is preserved: credential-bearing plaintext
HTTP is allowed only for numeric loopback addresses or exact `localhost`;
other hosts require HTTPS. Validation happens before the SDK is loaded.
Tools carry separate provider-safe `name` and native `canonicalId` fields;
the latter is captured in the tool callback rather than inferred from underscores.
The owned process must set `PI_OFFLINE=1` and `PI_TELEMETRY=0` before importing
the SDK. These flags are not an OS sandbox or a ban on the configured model API.

Use the returned `prompt(text)` method to preserve literal user input with
`expandPromptTemplates:false`. The `session` is the trusted wrapper's access to
SDK subscriptions, abort, compaction and context; upstream objects must not be
forwarded directly to a public product protocol. `diagnostics()` exposes only
version, selected tool names and counts of denied ambient-auth queries.

The deterministic tests run the real SDK against a loopback Chat Completions
fixture. Host responses and asset IDs are fixtures: this is not a native NDVI or
GUI delivery test, and it makes no remote model requests. They cover streamed
tool parameters, structured host results, denied defaults/discovery, literal
input, model/tool cancellation, SDK compaction and disabled automatic retries.

After preparing the patched dependency stage, run with its pinned Node:

```powershell
$env:SICNU_TEST_PI_NODE_MODULES='ABSOLUTE_STAGE/libexec/sicnu-geo-rs/pi-agent/node_modules'
& 'ABSOLUTE_STAGE/libexec/sicnu-geo-rs/node/node.exe' --test --test-concurrency=1 pi/product-runtime/test/controlled-session.test.mjs
```

The peer `auth-context.test.mjs` separately proves the vendor patch's original
red case. P0 completion requires both tests and the runtime-preparation receipt;
passing this module alone does not enable the product Pi backend.

## Installed private protocol (P1)

The sole installed entry is `libexec/sicnu-geo-rs/pi-product/main.mjs`, launched
with the bundled `libexec/sicnu-geo-rs/node/node.exe` and no CLI arguments.
It derives the installation prefix from its own module URL, reads the product
and SDK receipts under `share/sicnu-geo-rs`, verifies the product wrapper files,
SDK receipt bytes, recipe digest and pinned versions, then resolves the sibling
`pi-agent/node_modules`. It does not accept an SDK path or upstream Pi methods
from the peer. The native owner must verify and contain the child before
transmitting credentials. This wrapper adds no native task executor.

Stdin and stdout carry UTF-8 JSON Lines, one JSON object per physical line.
Invalid UTF-8, incomplete final lines, lines larger than 2 MiB before parsing,
unknown commands/fields and stale identities are rejected. Outbound frames
also have a 2 MiB limit. Stdout contains only protocol frames. Dependency
console diagnostics are replaced by fixed, bounded stderr messages; raw
frames, credentials and provider errors are never diagnostic output.

Every frame has this envelope:

```json
{"protocol":"sicnu.pi/1","sessionId":"owned-session","generation":"opaque-epoch","runId":"run-id","seq":1,"type":"prompt","text":"Create an NDVI change mask"}
```

`sessionId` and `generation` are immutable nonempty strings for the process;
`generation` is an opaque string, never a JSON integer. IDs are at most 256
UTF-8 bytes and contain no control characters. Each direction has its own
sequence, beginning at 1 and advancing by exactly one. `runId` is empty for
initialize, ready and shutdown; prompt creates a new, nonempty run ID which
cannot be reused. Run commands and events carry the current run ID. Global
errors and closed events may carry an empty run ID. Only one prompt runs at
a time, and P1 has no steering, automatic retries, replay or persisted history.

| Command | Additional required fields | Meaning |
| --- | --- | --- |
| `initialize` | `cwd`, `profile`, `systemPrompt`, `tools`, `toolRevision`, `sdkReceiptSha256` | First frame only, empty run ID. Load the controlled SDK session. |
| `prompt` | `text` | Start the run; literal text is at most 64 KiB UTF-8. |
| `tool.result` | `callId`, `result` | Acknowledge an exact pending call in this session, generation and run. `result` is a JSON object of at most 512 KiB. |
| `cancel` | none | Request SDK abort and native tool cancellation. |
| `shutdown` | none | Empty run ID; abort and close the process. |

`profile` has exactly `baseUrl`, `model`, `apiKey`, `contextWindow` and
`maxTokens`. The in-memory key arrives only in initialize, never argv, a
configuration file or an environment variable. `tools` is an immutable array
of `{name,canonicalId,description,parameters}`, with an object JSON Schema for
`parameters`. `toolRevision` is the native owner's opaque catalog snapshot
identity, retained alongside this array and returned on every tool request.
The native owner checks it against its current catalog. Node independently
compares `sdkReceiptSha256` against its installed, verified SDK receipt;
that value is not accepted merely because the peer supplied it. A tool result
has no `approved` field or approval semantics: approval remains a native
responsibility before executing a tool.

| Event | Additional fields |
| --- | --- |
| `ready` | `nodeVersion`, `piVersion`, `sdkReceiptSha256`, `toolRevision` |
| `content`, `reasoning` | `text` |
| `tool.request` | `callId`, `canonicalId`, `name`, `arguments`, `toolRevision` |
| `tool.cancel` | `callId` |
| `model.request` | no payload; one provider invocation attempt, including compaction or failure before usage |
| `usage` | `usage`, `cumulativeUsage`, each containing `input`, `output`, `cacheRead`, `cacheWrite`, `totalTokens` |
| `compaction` | `stage` (`started`, `completed`, `failed`), `reason` |
| `settled` | `aborted`, `status` (`completed`, `cancelled`, `failed`), optional `errorCode` |
| `error` | `code`, generic `message` |
| `closed` | `reason` |

Host tools execute sequentially, with a defensive pending-call cap of 16 and
a 1 MiB argument limit. Cancel emits `tool.cancel` for pending native work.
Even if the SDK has already settled, the wrapper withholds `settled` until
the matching `tool.result` acknowledges each pending call. Shutdown, stdin
EOF and disconnection abort the owned SDK work and close the process; the
native owner must cancel/reconcile its Task Center executions. A Node exit is
not evidence that a native task stopped or that assets were committed.

Each run permits 16 model requests, including automatic compaction, and 32
tool attempts, including unknown tools and parameter refusals. The wrapper
decorates Pi 1.5.2's documented public `Agent.streamFunction` for request
preflight and observes the original stream's single completion for usage.
It decorates the returned `AssistantMessageEventStream.push` to observe
producer activity, including summaries that call only `result()` and never
iterate. The original iterator remains untouched. It neither issues a second
request nor consumes SSE twice. An awaited public
`Agent.subscribe` listener stops tool attempt 33 before SDK tool preparation.
The wrapper therefore blocks model request 17 and tool execution 33 before
those operations begin. These seams depend on the pinned SDK contract.

Returned usage is counted once, including compaction. A run stops after a
reported cumulative 131072 total tokens or 16384 output tokens exceeds its
budget, and subsequent requests are blocked at the threshold. Each request's
output allowance is capped by the remaining output budget. Usage accounting
uses the provider's completed response; it cannot retract tokens a provider
already consumed or force an uncooperative provider to obey its allowance.
Model inactivity is limited to 120 seconds. The wrapper's `executionMs` timer
limits Node/model-active time to 15 minutes. It pauses throughout an outstanding
native `tool.result`, covering both approval and native execution; this is not
the full product run deadline. P2's native tool owner must enforce the full
15-minute run budget with only approval waiting paused, and cancel owned work
when it expires. P1 has no approval/running distinction or additional command.

The protocol tests have a lightweight boundary suite and opt-in real SDK
tests. The latter prove exact request-count budgets against a local HTTP
fixture, and drive actual SDK automatic compaction with streamed summaries
and a virtual clock through the private protocol. The installed-entry test starts the relocated bundled Node and
wrapper with an empty PATH, exchanges only private JSONL, and delivers a
fixture asset through a real SDK tool loop. Its asset is a test fixture,
not a scientific NDVI output or a GUI delivery assertion.

```powershell
$env:SICNU_TEST_PI_NODE_MODULES='ABSOLUTE_STAGE/libexec/sicnu-geo-rs/pi-agent/node_modules'
$env:SICNU_TEST_PI_PRODUCT_PREFIX='ABSOLUTE_RELOCATED_PREFIX'
& 'ABSOLUTE_RELOCATED_PREFIX/libexec/sicnu-geo-rs/node/node.exe' --test --test-concurrency=1 pi/product-runtime/test/protocol.test.mjs
```

Omitting these environment variables intentionally skips the corresponding
real SDK tests; skipped tests are not evidence of installed integration.

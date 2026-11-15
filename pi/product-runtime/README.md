# Controlled product Pi session (P0)

This module assembles the pinned, patched Pi 1.5.2 SDK using explicit model,
memory-only settings/credentials/session storage, an empty resource loader,
and only the host tool definitions supplied by the caller. It neither starts
`exp-rs --mcp` nor executes Python or filesystem tools. The future Qt adapter
must own tool approval, execution, cancellation acknowledgement and asset commit.

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

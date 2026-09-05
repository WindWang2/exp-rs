# Crash Isolation & Trust Classification

## Isolation tiers

| payload tier | process boundary | crash behaviour |
|---|---|---|
| Native C++ plugin | in-process | **can crash the host** — hence the trust classification and lazy loading below |
| Python plugin / operator | isolated worker process pool | worker crash ≠ GUI crash; pool restarts the worker with backoff, replays in-flight requests, plugin state rebinds |
| External process operator | its own session/process group | killed on timeout/cancel; host unaffected |
| Model runtime plugin | in-process execution, load-gated | load failures are diagnostics; inference failures raise into the operator error path |

## Native plugin risk controls

In-process native plugins cannot be made crash-proof — exp-rs controls the
risk instead of pretending:

1. **Trust classification** (see permissions.md): builtin → third-party →
   untrusted origins.
2. **Lazy by default, explicit when eager**: the CLI and every catalog
   surface (Processing, MCP, Workflow) work purely from manifests and dlopen
   a binary only when a contribution actually executes. The GUI shell is the
   one deliberate exception — it eagerly loads validated native plugins so
   docks/menu contributions exist at boot; loading remains
   diagnostics-gated and policy-gated.
3. **`SICNU_PLUGIN_DISABLE_NATIVE_THIRD_PARTY=1`**: hard kill-switch to run
   third-party-free sessions (used by the conformance kit and support
   diagnostics).
4. **Registry hygiene**: unload calls `shutdown()` and `dlclose`s; the
   runtime host revokes every contribution (operators, adapters, providers,
   executors) in the same critical section, so no dangling factory survives
   an unload.

## Optional plugin-host worker (roadmap)

An out-of-process native plugin host (exe-side shim loading third-party
binaries over an IPC protocol mirroring the Python worker's) is the designed
follow-up for full native isolation. The V1 interfaces were shaped so this
is additive: a new entry point (`EXPRS_createPluginHostWorkerV2`-style) plus
an IPC transport — no change to PluginV1.

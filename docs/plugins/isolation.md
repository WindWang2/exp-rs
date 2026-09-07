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
4. **Registry hygiene**: unload revokes every contribution (UI
   contributions, operators, adapters, providers, executors) BEFORE
   `shutdown()` and `dlclose`, so no dangling factory, widget or action
   survives the unmapping of the plugin binary.
5. **Unload safety (ADR 0130)**: unload is a barrier-protected sequence —
   arm the drain (new dispatch refused with a typed failure), wait bounded
   (`SICNU_PLUGIN_UNLOAD_TIMEOUT_MS`, default 30 s) for in-flight
   executions, release UI through the shell sink, revoke registrations,
   `shutdown()`, delete the instance, `dlclose` LAST. A timeout REFUSES the
   unload (stable diagnostic `E4005 PluginInUse`) — plugin code is never
   unmapped under an executing thread. `unloadAll` at host shutdown leaves a
   still-busy plugin mapped for process exit instead of forcing it out.
6. **UI reverse ownership**: plugin docks/menu actions/preferences pages are
   attached and released through a shell sink
   (`src/app/plugin_shell_ui.*`), so enable/disable is a real round-trip
   without restart and no plugin widget outlives its binary. GUI exit
   unloads plugins before QApplication destruction (CLI `ShutdownGuard`
   parity).
7. **Load-path containment**: the manifest `entrypoint` must resolve to a
   regular file inside the canonical plugin root — absolute paths, `..`
   components and symlink escapes are rejected at validation AND re-checked
   immediately before `dlopen`/`LoadLibrary` (ADR 0130, issue #756).

## Python contribution revocation

Python plugins run in the isolated worker pool; their `py:` algorithm
registrations are revoked by the host bridge on plugin unload (the atomic
catalog and the provider map drop every id the plugin registered, and the
worker daemon pops the plugin's executors from the reused process). A reload
re-registers cleanly — no dead entries, no duplicates (issue #755).

## Optional plugin-host worker (roadmap, deliberately deferred)

An out-of-process native plugin host (exe-side shim loading third-party
binaries over a versioned RPC, mirroring the Python worker's isolation) is
the DESIGNED follow-up for full native crash isolation: plugin crash ≠ GUI
crash, timeout can kill the host process, reload is a process restart, and
contribution metadata stays discoverable without mapping code in the GUI.
It is intentionally NOT implemented in V1: the in-process lifecycle above is
its precondition, and no partial isolation layer ships. A full-UI host
would additionally need out-of-process widget transport; the first host
slice scopes to operator/model/data/agent contributions. The V1 interfaces
were shaped so this is additive: a new entry point
(`EXPRS_createPluginHostWorkerV2`-style) plus an IPC transport — no change
to PluginV1.

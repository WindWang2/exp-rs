# Host-Process Plugin Runtime (Isolation Runtime 5.0)

Native plugins can opt into running in a dedicated worker process
(`exprs_plugin_host_worker`) instead of inside the ExpRS host. A plugin
crash, hang or malformed output then degrades to typed diagnostics and a
bounded recovery — the host process stays up.

## Opting in

```json
{
  "manifest_version": 1,
  "id": "org.example.heavy",
  "runtime": "host-process",
  "entrypoint": "libheavy.dll",
  "entrypoint_kind": "native",
  ...
}
```

- `runtime` defaults to `in-process` (the historical behavior).
- `runtime: "host-process"` + a `ui` section is REFUSED at validation: the
  first host slice covers operator / agent-tool / data-provider /
  model-runtime contributions. Full out-of-process UI transport remains
  future work (see isolation.md).
- `SICNU_PLUGIN_HOST_PROCESS=off` refuses host-process plugins typed
  (E6006) instead of loading them in-process — there is never a silent
  downgrade.

## Wire contract (protocol 1.1, additive over 1.0)

- Transport: length-prefixed JSON frames (u32 LE + payload, 32 MiB default
  cap) over two inherited OS handles passed in argv — never stdio, so
  plugin `printf` noise lands on a sink and cannot corrupt framing.
- Versioning: fourth axis (`EXP_RS_HOST_PROTOCOL_VERSION`). Same major
  required; peer minor ≤ local minor. Violations are refused E6001 BEFORE
  any plugin code runs, together with an API/ABI gate between host and
  worker SDKs.
- Methods: `plugin.load`, `operator.execute`, `agentTool.execute`,
  `dataProvider.discover|inspect|open`, `modelRuntime.load|infer`,
  `plugin.shutdown`; worker→host `progress`/`event` frames only (services
  are materialized at load — no reverse calls in v1).
- Bounded payloads: large artifacts stay workspace-contained file
  references; tensor exchanges are bounded JSON. The frame cap is the
  enforcement point (E6003).
- Structured errors: one taxonomy for both runtimes (E6001–E6009, see
  `exprs/plugin_diagnostics.h`). The Python worker transport surfaces the
  same codes (E6004/E6005/E6006/E6009) from its bridge.

## Concurrency (protocol 1.1, plugin-platform 8.0)

The host keeps up to `maxRequestConcurrency` requests in flight per worker
(enforced exactly by a FIFO-fair session gate; overflow refuses typed
E6007 after a bounded wait). The worker dispatches execution requests
(operator / agent tool / data provider / model runtime) on a bounded pool
(width negotiated down from the host quota through `plugin.load`
"limits"; hard-capped at 8); lifecycle requests stay serialized. Per-id
cancel frames replace the broadcast-only v1 cancel, and a host-side
cooperative cancel now reaches the plugin's operator context. Timeout
escalation is precise: a timed-out request is cancelled by id; a worker
that keeps serving peers is POISONED and killed when its last in-flight
request drains (sole-request timeouts escalate to the kill ladder
directly). A hung operator can therefore delay one request, never tear
down peers. Progress frames are coalesced per request (>= 20 ms window)
and the pending-event queue on the host is bounded with a drop counter.
The frame cap negotiates downward from the quota's `maxResponseBytes`.

## Declarative UI (protocol 1.1)

`runtime: "host-process"` plugins may declare UI contributions through
the declarative schema route: export `EXPRS_createUiSchemaProviderV1`
(`exprs/plugin_ui_schema.h`), declare a `ui` section and `access.ui =
true`. The worker validates the schema (fail closed, hard-capped) and
answers `ui.describe`; `ui.invoke` carries bounded control events and
returns bounded state patches. The host renders every widget itself —
see [declarative-ui.md](declarative-ui.md). Raw widget transport does
not exist and will not be invented.

## Quotas & enforcement honesty

Per-plugin quotas (manifest `quotas`, clamped to host ceilings from
`SICNU_PLUGIN_QUOTA_*`):

| quota | Windows | POSIX |
|---|---|---|
| maxRequestConcurrency | protocol v1 serializes per plugin (session mutex, effective 1); the quota value is advisory | same |
| requestDeadlineMs | host-side ceiling + kill ladder (exact) | same |
| maxResponseBytes | frame cap (exact) | same |
| workerMemoryBytes | job object limit (exact) | RLIMIT_AS best effort (coarse) |
| workerCpuRatePercent | job object CPU rate control | not enforced (advisory) |
| maxChildProcesses | job object ActiveProcessLimit (exact) | advisory |
| gpuHint | advisory | advisory |

On timeout: cancel frame → grace → `TerminateJobObject`/`SIGKILL`. Windows
has no SIGTERM for arbitrary console processes; the ladder is therefore a
forced kill there (exitSignal 9 marker on both platforms).

## What this is NOT

- **Path policy is not an OS sandbox.** Filesystem roots declared under the
  manifest `access` object are enforced at the worker's HostServices
  boundary and audited elsewhere; native code in the worker can still touch
  the world outside its grants unless the OS primitives above also apply.
- Network access of plugin code is not intercepted on either runtime.
- In-process plugins keep the declaration + audit model only.

## Lifecycle & recovery

- load = spawn → handshake → `plugin.load` → proxy contributions registered
  into the ordinary registries (AtomicAlgorithmRegistry etc. unchanged; no
  second scheduler).
- unload = drain (E4005 refusal while busy, identical to in-process) →
  revoke proxies → `plugin.shutdown` → reap → kill ladder on refusal.
- crash = EOF on the channel → in-flight calls fail E6005 → the proxy
  applies the restart policy ONCE (respawn + reload, atomic arm so a
  crash-looping plugin cannot stampede) → further calls fail typed until
  the policy window clears or the plugin is unloaded/reloaded.
- Unload ordering is provably UAF-free by construction: proxies never hold
  plugin memory (unlike the in-process path); sessions are shared_ptr
  owned and the worker process owns all plugin objects.

## Migration notes for plugin authors

1. Keep the `PluginV1` implementation unchanged — the worker reuses the V1
   entry point; isolation is process-level, not ABI-level.
2. Add `"runtime": "host-process"` (and drop `ui` if present).
3. Declare `access` roots if you read/write files; declare `quotas` to
   lower your own limits (raising them beyond policy is clamped).
4. Operators must honor cooperative cancellation (the kill ladder is a
   forced crash after the deadline — cancelling cleanly keeps the worker
   alive).

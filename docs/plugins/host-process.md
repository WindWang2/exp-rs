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

## Wire contract (protocol 1.0)

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

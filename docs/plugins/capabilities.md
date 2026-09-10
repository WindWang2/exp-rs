# Capability Declarations & Quota Enforcement (honest matrix)

This page consolidates what the manifest `access` object (structured capability
declarations) and the `quotas` object actually enforce on each runtime. The
governing principle: **never claim enforcement that does not exist.**

## The `access` object

```json
{
  "access": {
    "filesystem": { "read": ["${workspace}/inputs"], "write": ["${temp}"] },
    "network": true,
    "externalProcess": true,
    "gpu": { "hint": "cuda" },
    "workspace": { "mutate": false },
    "project": { "mutate": false },
    "modelProvider": { "frameworks": ["onnx"] },
    "ui": true,
    "destructive": false
  }
}
```

- Omitted fields parse to **deny-by-default**; a null `access` object is deny-all.
- Path placeholders `${plugin}`, `${workspace}`, `${temp}` are expanded by the
  host, never by the plugin. Unresolvable declarations are validation ERRORS
  (fail closed), not warnings.
- Roots are canonicalized; symlink escapes are rejected.

### Enforcement by runtime

| declaration | host-process runtime | in-process runtime |
|---|---|---|
| `filesystem.read/write` roots | enforced at the worker boundary: operator `workDir` and materialized service paths are validated against the declared roots (see below); NOT an OS sandbox — plugin code inside the worker is native and unconstrained | declaration + audit + diagnostics only |
| `network` | **not intercepted** (no claim) | not intercepted |
| `externalProcess` | gates the external-tool operator path in the host framework; worker-side plugin code is native and unconstrained | same |
| `gpu.hint` | advisory passthrough to model runtimes | advisory |
| `workspace.mutate` / `project.mutate` | declaration consumed by host surfaces that mutate workspace/project state; no blanket interception | same |
| `modelProvider.frameworks` | constrains which frameworks a plugin may register | same |
| `ui` | legal only with a declarative schema provider (plugin-platform 8.0); raw widget transport does not exist | Qt `UiContributionV1` (build-locked) |
| `destructive` | declaration surfaced to consent/audit surfaces | same |

## The `quotas` object

Every declared value is **clamped to the host ceilings** (`SICNU_PLUGIN_QUOTA_*`
environment overrides); a manifest can lower its own limits but never raise
them. Exceeding a quota is a typed refusal (E6007 family), never host
degradation.

| quota | Windows | POSIX (Linux) | macOS |
|---|---|---|---|
| `maxRequestConcurrency` | **exact** — host-side FIFO gate per session (plugin-platform 8.0); the worker dispatches up to the negotiated width | same | same |
| `requestDeadlineMs` | **exact** — host-side per-request ceiling; timeout runs the cancel/kill ladder | same | same |
| `maxResponseBytes` | **exact** — negotiated frame cap: the worker clamps writes to the host's cap; a frame over the cap is E6003 and tears the channel down | same | same |
| `workerMemoryBytes` | **exact** — job object process memory limit | best-effort `RLIMIT_AS` before exec (coarse; address-space, not RSS) | best-effort `RLIMIT_AS` (not locally exercised; macOS lane is configure+build only) |
| `workerCpuRatePercent` | **exact** — job object CPU rate control (hard cap) | not enforced (advisory) | not enforced (advisory) |
| `maxChildProcesses` | **exact** — job object ActiveProcessLimit | advisory (RLIMIT_NPROC is per-user; not applied) | advisory |
| `gpuHint` | advisory | advisory | advisory |

### Kill ladder (deadline / cancel escalation)

1. `cancel` frame for the offending request id (worker sets the request's
   cooperative-cancel flag);
2. 3-second grace — a clean cooperative cancel keeps the worker alive;
3. forced kill. POSIX kills the worker's **process group** (`setpgid` at
   spawn), so worker-spawned grandchildren die too; Windows terminates the job
   object (every process in it). Exit marker: signal 9 on POSIX, terminated
   job on Windows.

With multiple in-flight requests (plugin-platform 8.0) a timed-out request is
cancelled and — if the worker stays cooperative — abandoned typed (E6004)
while peers continue; a worker that survived a forced-abandonment timeout is
marked poisoned and is killed+respawned once its last in-flight request
drains. Resource exposure stays bounded: at most `maxRequestConcurrency`
concurrent requests, each at most `requestDeadlineMs`.

## What this is NOT

- **Path policy is not an OS sandbox.** Native code inside the worker can
  still touch the world outside its grants unless an OS primitive above also
  applies. The roots gate the seams the host controls (workDir, services,
  external-tool path policy).
- **Checksums are integrity, not authenticity.** A declared checksum detects
  corruption; it does not identify a signer.
- Network access of plugin code is not intercepted on either runtime.
- In-process plugins keep the declaration + audit model only.

See [permissions.md](permissions.md) for the trust classification and the
capability → permission implication table, and [host-process.md](host-process.md)
for the wire contract.

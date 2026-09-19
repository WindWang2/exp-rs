# Host–worker trust boundary, ownership & lifetime contracts

Scope: the out-of-process plugin host (`src/plugins/host/`), its SDK
transport (`src/sdk/exprs/ipc_*`, `external_process.*`) and the declarative
UI renderer (`src/plugins/framework/plugin_ui_schema_host.*`). This is the
normative summary the regression suites (`test_plugin_host_process`,
`test_exprs_ipc`, `test_plugin_ui_schema_host`, `test_exprs_external_process`)
enforce. Issue references: #1036, #1038, #1039, #1040, #1041.

## 1. Who is trusted

| Side | Trust | Rule |
|---|---|---|
| Host (launcher, GUI) | trusted | Owns every widget, every policy decision, every typed error |
| Worker process | **untrusted** | Everything it sends is remote input, even when "our" code runs inside it |
| Plugin code | untrusted | Runs in the worker; never sees a host pointer |

The worker validates what it can (defense in depth for well-behaved plugins),
but the host NEVER relies on that. Every worker→host payload is re-validated
at the process boundary.

## 2. Validation matrix

| Payload | Validated in worker? | Validated in host? | Refusal shape |
|---|---|---|---|
| `worker.hello` | n/a (worker authors it) | yes, in `awaitHandshake` (protocol/api/abi, typed fields) | spawn fails typed (E6001/E6002/E5002) |
| `plugin.load` registration report | no | host iterates only typed arrays; capability gates (`modelFrameworkAllowed`) apply | load fails typed, registrations revoked |
| `ui.describe` schema | yes | **yes, again** in `PluginHostProcessRuntime::describeUiSchema` (`validatePluginUiSchema`, capped recursion/controls) and again in `PluginUiSchemaRenderer::attachPluginSchema` | `{ok:false, code:"E5005", details:[...]}` — never a rendered tree |
| `ui.invoke` response | no | extraction + per-value type guards (`uiStateFromInvokeResponse`, `applyValue` guards) | hostile values ignored, `eventApplied` still fires |
| Any JSON frame | n/a | frame caps (shared/per-direction), envelope decode inside try/catch | E6002/E6003, channel torn down |

Rules of thumb:

* `Json::Value::as*()` is only legal after an `is*()` guard **or** inside a
  `catch (const Json::Exception&)` that maps to a typed diagnostic. The
  external-JSON readers in `src/sdk/exprs/json_reader.h` implement the guard
  half of this rule for manifests, packages, workflow documents and the
  discovery index cache (#1038).
* Unknown fields are ignored (additive evolution); wrong-TYPED known fields
  are refused. Missing optional fields keep their default.

## 3. Descriptor / handle ownership (#1036)

Single-owner rule: **the `IIpcStream` owns both pipe ends it was constructed
with.** `IpcChannel` owns the stream; the session owns the channel.

* `makeIpcHandleStream(read, write)` transfers ownership of both handles to
  the returned stream on POSIX (fds) and Windows (HANDLEs).
* `IIpcStream::close()` is idempotent: an atomic exchange elects exactly one
  closer, which releases both ends. The destructor closes whatever remains.
* `IpcChannel::close()` defines the release ORDER: mark closed → notify
  waiters → join the reader thread → `stream->close()`. The reader is never
  alive while a descriptor is being closed (a recycled fd/handle can
  otherwise be read by the exiting reader).
* `PluginHostProcessSession` never closes the parent-side pipe ends itself:
  every lifecycle path (`shutdown`, `killProcess`, destructor,
  crash-confirm) funnels through `IpcChannel::close()`.
* Failure paths in `spawnWorkerProcess` still close everything they created
  before returning false. On Windows the launcher closes its copies of the
  CHILD-side ends right after `CreateProcessW`; holding them would defeat
  EOF-based crash detection.

Regression evidence: `test_plugin_host_process` "host session releases pipe
handles across shutdown/kill cycles" (real worker, handle count flat over
graceful + crash + registry respawn cycles) and `test_exprs_ipc` "handle
streams own and release their descriptors exactly once".

## 4. Process lifetime & shutdown contracts

* **spawn** → handshake (bounded by `handshakeTimeoutMs`) → `plugin.load`
  (control plane). A failed handshake kills the fresh worker before any
  plugin code runs.
* **request** (data plane): deadline clamped to quota; FIFO concurrency gate
  (E6007 on overflow); per-id cancel frame on timeout; grace window; then
  either direct kill (sole in-flight) or poison (killed when the last
  in-flight request drains).
* **crash**: channel EOF → `confirmProcessDeath()` probes with
  `waitpid(WNOHANG)` / `WaitForSingleObject(0)`; exactly one closer wins the
  liveness exchange. The runtime applies the bounded restart policy.
* **shutdown**: `plugin.shutdown` reply → bounded wait for self-exit →
  process-group/job reap → handle release. A concurrent killer that already
  won the liveness exchange owns the release; the loser must not close again.
* **worker death / host destruction**: pending requests fail typed E6005;
  unload revokes registrations before the binary goes away; the session
  destructor always kills + releases.

## 5. External process parity (POSIX ⇄ Windows)

`ExternalProcess::run()` has one contract on both platforms:

* output is drained until both pipes hit EOF, **but** the direct child's exit
  is detected independently of pipe EOF (`waitpid(WNOHANG)` on POSIX,
  `WaitForSingleObject` on Windows);
* after the direct child exits, a bounded post-exit drain window
  (`postExitDrainGraceMs`, default 2 s) collects buffered tail output;
* a descendant that inherited the pipe write ends can therefore never stall
  `run()` to the full timeout nor turn a successful run into `timedOut`;
* when the grace expires with pipes still open, the remaining process group /
  job is reaped (POSIX `SIGKILL` group after the WNOHANG reap; Windows job
  close), so survivors do not leak as orphans;
* the timeout/cancel ladder is unchanged and cannot fire after the child has
  already exited.

Regression evidence: `test_exprs_external_process` "descendant-held pipes
never stall run() or fake a timeout" plus the hung-child timeout guard.

## 6. Typed error surface (selection)

| Situation | Code |
|---|---|
| Worker UI schema fails host validation / UI refused by policy | E5005 |
| Host-rendered ui event fails pre-send validation | E6010 |
| Frame cap violated | E6003 |
| Malformed envelope / protocol violation | E6002 |
| Worker gone / never spawned | E6005 |
| Request deadline (kill ladder applied) | E6004 |
| Concurrency quota saturated | E6007 |
| Worker exposes no declarative UI | E6008 |
| Cancelled before completion | E6009 |

## 7. Declarative UI response contract (#1040)

`UiInvokeDelegate::invoke()` returns the HOST INVOKE RESULT, i.e.
`PluginHostProcessRuntime::invokeUi`'s output:

```json
{ "ok": true, "response": { "state": { "<controlId>": <value> } } }
```

The renderer extracts the patch with `exprs::uiStateFromInvokeResponse`
(state lives at `result["response"]["state"]`, **not** `result["state"]`).
A delegate returning anything else simply applies no state — that is the
documented no-op, not an error. `test_plugin_host_process` proves the real
worker shape end to end; `test_plugin_ui_schema_host` proves widget
application with the same shape and that hostile state values are ignored.

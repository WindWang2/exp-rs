# Plugin Platform 9.0 — Capability & Gap Matrix (baseline truth, pre-implementation)

Legend for enforcement levels (used verbatim in code/docs/CLI output):
- **enforced-host** — refused/limited in the launcher process, cannot be
  bypassed by plugin code (exact on all platforms).
- **enforced-worker** — refused in the worker process at the seam the host
  hands plugin code (exact for that seam; NOT an OS sandbox).
- **enforced-os** — kernel-enforced bound (Windows job object; POSIX
  RLIMIT_AS; exactness documented per platform).
- **advisory** — declared + surfaced + logged; NOT enforced anywhere.
- **audit-only** — recorded in diagnostics/doctor; no gate.
- **refused-by-contract** — we do not claim it; documented non-goal.

## What master already has (8.0, verified by reading the code)

| Capability | Level today | Where |
|---|---|---|
| protocol version axis (1.0/1.1) | enforced-host | handshake gate E6001 before plugin code (`plugin_host_session.cpp` awaitHandshake) |
| frame cap, single shared bound | enforced-host+worker | `IpcFrameLimits` writer+reader; `lowerFrameCap` monotonic both directions |
| maxRequestConcurrency quota | enforced-host | FIFO `ConcurrencyGate`, typed E6007 overload refusal |
| requestDeadlineMs | enforced-host | session clamp + per-id cancel + kill ladder |
| workerMemoryBytes | enforced-os | Windows job object; POSIX pre-exec RLIMIT_AS (+ parent-side hard-limit sanity warning) |
| workerCpuRatePercent | enforced-os (Windows) / advisory (POSIX) | job CPU rate control; RLIMIT_NPROC is per-user → honest advisory |
| maxChildProcesses | enforced-os (Windows) / advisory (POSIX) | job ActiveProcessLimit; POSIX documented advisory |
| workDir containment vs declared write roots | enforced-worker (opt-in: only when plugin DECLARES write roots) | `WorkerPolicy::allowsWorkDir`, typed E5005 |
| progress/event boundedness | enforced-host+worker | worker ≥20 ms coalescing; host event queue cap 1024 + drop counter |
| crash isolation + bounded restart | enforced-host | respawn policy 3/60 s, atomic re-arm |
| process-tree cleanup | enforced-os (POSIX pg kill; Windows job kill-on-close) | ladder/crash/shutdown paths |
| declarative UI schema validation | enforced-worker + enforced-host | `validatePluginUiSchema` hard caps; host renderer via UiShellSink |
| install checksum | enforced-host | self-contained SHA-256, known-answer pinned |
| staged install + rollback | enforced-host | atomic swap, previous-good preserved |
| signature authenticity | refused-by-contract | metadata carried, integrity-not-authenticity (documented) |
| network interception | refused-by-contract | documented non-goal for both runtimes |

## 9.0 gaps (each: evidence → work package)

| # | Gap (evidence) | Milestone | Plan |
|---|---|---|---|
| G0.1 | One shared frame cap: quota `maxResponseBytes` lowers BOTH directions (`ipc_channel.cpp lowerFrameCap` doc), so a small response quota also caps host→worker request frames; no per-direction negotiation field | M0 | per-direction caps (`maxRequestBytes`/`maxResponseBytes`), protocol minor 1.1→1.2 additive, feature-negotiated |
| G0.2 | No mechanical whole-manifest round-trip test (8.0 fixed three silently-dropped fields; nothing prevents a fourth) | M0 | exhaustive field-by-field round-trip + unknown-key tolerance tests |
| G0.3 | Malformed-frame coverage is pointwise; no structured fuzz loop (seeded, bounded) over the frame/envelope codec | M0 | seeded fuzz harness in `test_exprs_ipc` (deterministic, bounded) |
| G1.1 | Capability declarations parsed but host-side proxy layer does not consult them: `ui.describe` served regardless of `access.ui`; model load served regardless of `access.modelProvider.frameworks` | M1 | proxy/runtime-host gates (enforced-host, typed E5005) + retention of parsed access in the session entry |
| G1.2 | Pure-manifest external tool operators spawn processes regardless of `access.externalProcess` (framework path) | M1 | gate at `ExternalToolOperator` (enforced-host) with typed refusal |
| G1.3 | dataProvider URIs unchecked against declared `schemes` (manifest declares them; worker accepts any URI) | M1 | worker-side scheme gate (enforced-worker, opt-in semantics preserved: empty declared list = unrestricted, documented) |
| G1.4 | No machine-readable declaration-vs-enforcement matrix | M1 | `capabilityEnforcementMatrix()` + `plugin inspect --json` field + debug bundle |
| G2.1 | Gate has no queue-depth observability; diagnostics snapshot lacks per-plugin in-flight/waiters/dropped/restarts | M2 + M10 | bounded-wait stats + enriched snapshot |
| G2.2 | No interleaved timeout×cancel×crash stress evidence (rendezvous-based, repeatable) | M2 | stress test, no sleeps-as-correctness |
| G3.1 | Orphan grandchildren reaped, but no explicit orphan-detection surface (kill(-pgid,0) probe) | M3 | probe + diagnostic + test |
| G4.1 | Handshake carries no feature list; new protocol surface cannot be negotiated, only versioned | M0/M4 | `features` array in worker.hello (1.2), host consumes |
| G4.2 | Fixtures cover operator+UI+misbehavior; no single fixture exercising ALL contribution kinds over the worker path | M4/M5 | kitchen-sink fixture plugin |
| G5.1 | Conformance kit has no PT_* for dataProvider / agentTool / modelRuntime surfaces | M5/M9 | three checks driven by manifest conformance declarations |
| G6.1 | `ui.invoke` event envelope forwarded host→worker without host-side validation/bounds (schema caps bound describe only) | M6 | host-side event validation (bounded ids/types/value size, typed refusal) |
| G6.2 | Schema has no accessibility metadata | M6 | optional capped `description`/`accessibilityLabel` fields (additive, validated) |
| G7.1 | Interrupted-install staging leftovers and corrupt-package paths lack tests; install-time dependency constraints unchecked | M7 | hardening + tests (previous-good never touched on failure) |
| G8.1 | No offline index/discovery surface (discovery scans roots, but no index artifact with compatibility filtering/pinned versions) | M8 | local-only `plugin index` (no online service) |
| G9.1 | PT_CANCEL/… skipped checks are reported as `ok:true` with detail "skipped…" — pass and skipped not machine-separable | M9 | `status: pass/fail/skipped` retained alongside `ok` |
| G9.2 | No PT_PROCESS_CLEANUP / PT_QUOTA / PT_PERMISSIONS checks | M9/M3 | worker-pid-gone + group-reaped check; overload-refusal check; workDir-refusal check |
| G10.1 | `plugin doctor` lacks per-plugin health (pid, generation, in-flight, restarts, last typed failure); no debug bundle; no secret redaction | M10 | health snapshot + `plugin debug-bundle` with redaction |

## Explicit non-goals (no security theater, no duplicate authority)

- No OS sandbox claim: worker-side gates bound the host-provided seams
  (workDir, provider URIs), never arbitrary plugin I/O. In-process runtime
  capability handling stays declaration + audit + diagnostics.
- No network interception claim (refused-by-contract, unchanged).
- No signature authenticity claim (no trust anchor introduced in 9.0).
- No plugin "marketplace" service: M8 is a local/offline index only.
- No second agent runtime / scheduler / renderer / data store.

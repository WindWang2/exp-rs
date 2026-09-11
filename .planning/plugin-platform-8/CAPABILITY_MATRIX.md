# Plugin Platform 8.0 — Capability Matrix (post-implementation)

| Capability | Status | Evidence / Notes |
|---|---|---|
| Host protocol versioning (4 axes) | Implemented (1.0→1.1 additive) | host_protocol.h + ipc_envelope.h lockstep; E6001 gate before plugin code; versioning.md |
| Multiple in-flight requests | Implemented | channel id-correlation (pre-existing) + worker ExecutionPool; rendezvous test peak ≥ 3 |
| Per-request concurrency quota (maxRequestConcurrency) | Implemented (was advisory) | ConcurrencyGate FIFO, exact; E6007 typed overload refusal; unit + integration |
| Request deadlines + kill ladder | Implemented (baseline) + precise escalation | sole-request timeout kills; peers-in-flight poison → kill at drain; poison test |
| Host-side cooperative cancel → plugin | Implemented (new in 1.1) | proxy forwards context.isCancelled as per-id cancel frame; test E6009 = 4000 |
| Worker-side cooperative cancel | Implemented (baseline for operators) | per-request flag map; broadcast id −1 preserved |
| Progress/events boundedness | Implemented | per-request ≥ 20 ms coalescing; host pending-event queue cap 1024 + drop counter |
| Frame-cap negotiation | Implemented (downward only) | limits.maxFrameBytes in plugin.load; lowerFrameCap monotonic; violating peer → E6003 |
| Crash isolation + bounded restart | Implemented (baseline) | proxy one-shot recovery; runtime restart policy; crash test |
| Process-tree cleanup | Implemented (POSIX: new) | setpgid + group kill on ladder/crash/shutdown; orphan test (grandchild reaped both paths); Windows job object (baseline, unchanged) |
| Worker memory bound | Implemented (POSIX: new RLIMIT_AS; Windows job object baseline) | pre-exec setrlimit; coarse/best-effort documented (capabilities.md) |
| workerCpuRate / maxChildProcesses POSIX | Declared-advisory (refused-by-contract) | RLIMIT_NPROC is per-user; documented, not claimed |
| Filesystem capability enforcement | Implemented (opt-in worker-side gate) | workDir vs declared write roots + temp; E5005; deny-by-default for declared plugins; NOT an OS sandbox (documented) |
| Network interception | Refused by contract | documented non-enforcement (both runtimes) |
| Declarative out-of-process UI | Implemented | plugin_ui_schema (validated, capped), ui.describe/ui.invoke, worker probe via optional entry point, host renderer through existing UiShellSink, revoke at unload; shell placement = workbench seam |
| In-process Qt UI contributions | Implemented (baseline) | UiContributionV1 + PluginUiHost; regression suites green |
| Lifecycle: drain/revoke/reload/generation | Implemented (baseline) + reload fix | installPluginOperator re-registers lazy adapter on reload (round-trip restoration); barrier suites green |
| Conformance kit | Implemented (13 checks) | PT_CANCEL/PT_CONCURRENCY/PT_RESTART/PT_UI_SCHEMA added; undeclared targets report skipped |
| Packaging: checksums / staged install / rollback | Implemented | self-contained SHA-256 (known-answer pinned); staging + atomic swap + rollback; SBOM/signature metadata carried, integrity-only |
| Signature authentication | Refused by contract | no trust anchor → metadata only; documented |
| Docs/diagnostics sync | Implemented | capabilities.md written (was dangling reference), host-process.md protocol 1.1, declarative-ui.md new, versioning.md 4-axis |

# Plugin Platform 9.0 — Test matrix & local evidence

Environment (all runs unless stated): Linux x86_64, kernel
6.18.49-2-lts, GCC /usr/sbin/c++ (C++20), Ninja, Debug, build parallelism ≤ 4,
test parallelism 1. GDAL 3.13.3, system Qt 6. No online CI waited on.

## Suites (owner scope; all must be green on the branch)

| Suite | Scope | Baseline (master f316dfdbb4) | Post-M0 | Post-all |
|---|---|---|---|---|
| test_exprs_ipc | frame/envelope/channel contract | see §Runs | see §Runs | pending |
| test_plugin_capabilities | access/quotas parsing & gates | see §Runs | pending | pending |
| test_plugin_manifest | manifest parse/round-trip | see §Runs | pending | pending |
| test_plugin_ui_schema | declarative UI schema contract | see §Runs | pending | pending |
| test_plugin_ui_schema_host | host-side schema renderer bridge | see §Runs | pending | pending |
| test_plugin_host_process | launcher↔worker end-to-end (isolation fixture) | see §Runs | pending | pending |
| test_plugins_runtime_host | runtime host bootstrap/adapters | see §Runs | pending | pending |
| test_plugin_execution_barrier | drain/lock barrier | see §Runs | pending | pending |
| test_exprs_plugin_system | packaging/install e2e (POSIX) | see §Runs | pending | pending |
| test_exprs_plugin_loader | in-process loader (hello fixture) | see §Runs | pending | pending |
| test_cli_commands_json | CLI plugin subcommands JSON contract | see §Runs | pending | pending |

## Baseline run (2026-09-12, this worktree, pre-change)

First attempt: suites not built (build of the fat lane had failed on
`src/geospatial/metadata/canonical_metadata.cpp` — pre-existing master issue
on GDAL 3.13.3, `GDALMDArrayRead` count param is `const size_t*` but master
passed `const GUInt64*`; fixed with a one-token change + comment, recorded as
a cross-lane baseline fix in REVIEW_LOG.md). Re-run recorded below once the
baseline targets finish building; recorded as run #R0.

## Runs log

### R0 — baseline (pending build)

### R-M0 — protocol 1.2 (directional caps, features, fuzz, quota field)

Planned additions to test_exprs_ipc (each asserts old-code-would-fail where
applicable):
- directional caps independent per direction (send beyond recv bound OK)
- send cap refuses oversized local frames (E6003 + channel teardown)
- recv cap refuses oversized peer frames (E6003 + ProtocolError outcome)
- lowerFrameCap shared semantics + monotonic per direction
- seeded fuzz: 4096 mutated envelopes — no crash, typed accept/reject
- seeded fuzz: 256 random raw frames — bounded reads, no crash
- version matrix incl. 1.2 vs 1.1/1.0 peers
test_plugin_capabilities: quota maxRequestBytes parse/clamp/toJson
test_plugin_manifest: exhaustive whole-manifest round-trip audit

## Timing-sensitive assertions

test_exprs_ipc cancel/timeout paths use generous margins (8.0 noted one
transient under heavy host load). If a transient reproduces, it is recorded
here with the load condition, never silently retried into green.

RECORDED TRANSIENT (2026-09-12): test_plugin_host_process
"concurrent requests run in parallel within the quota" failed once in a full
run ("restart policy exhausted" = a concurrent recovery was busy when a
caller's session snapshot was dead) while the host was building 3 other 9.0
tracks simultaneously (load average 23). The suite passed alone and the full
suite passed on re-run (163 assertions / 15 cases). The typed-failure-on-
busy-recovery behavior is by design (atomic respawnArmed collapse); the test
assumes the worker stays alive, which load starvation violated. Follow-up
candidate: make the assertion recovery-tolerant; not done in 9.0 because it
would weaken the crash-ladder regression.

## Explicitly NOT run on this host

- Windows job-object enforcement (memory/CPU-rate/ActiveProcessLimit):
  documented, not locally executable. Code path unchanged from 8.0 except
  where shared POSIX/Windows code was touched (compiled, not executed).
- macOS parity lanes: documented, not locally executable.
- Disk-full packaging behavior: not forced portably; covered indirectly by
  the unwritable-target test on POSIX (labeled, never claimed green).

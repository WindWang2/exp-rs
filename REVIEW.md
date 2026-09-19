# Independent review — plugin/sdk trust track

Scope reviewed: `git diff origin/master...HEAD` plus the new files
(`src/sdk/exprs/json_reader.h`, `tests/fixtures/hostile_ui_worker/`,
`docs/plugins/trust-boundary.md`). Method: one adversarial read-only review
subagent (no implementation context shared beyond the diff) plus a
self-review along correctness / thread-safety / resource ownership /
exception paths / cross-platform / performance / API-ABI / test quality.
Every finding was verified against the source before disposition.

## Findings and dispositions

| # | Severity | Finding | Disposition |
|---|---|---|---|
| R1 | P1 | `IpcChannel::close()` released the stream without `mWriteMutex`: a concurrent writer could still be inside `writeAll()` while the fd/handle was closed (recycled-descriptor hazard on POSIX, UB on Windows). | **Fixed** — new `releaseStream()` takes `mWriteMutex` before `mStream->close()` (lock order `mCloseMutex → mMutex(released) → mWriteMutex`, no inversion; the reader never takes `mWriteMutex`). |
| R2 | P2 | `HostProcessOperatorProxy::run()` read `outcome.result["result"]` on worker-controlled JSON with no object guard (assert/UB on a non-object result). | **Fixed** — non-object result now throws a typed `ComputationError`; agent-tool and provider proxies gained equivalent `E6002` refusals; model-runtime load guards its `backendName`/`deviceName` reads. |
| R3 | P2 | POSIX abandoned-drain path called `killProcessGroup(pid, SIGKILL)` after the child pid was reaped (undefined signal to a possibly recycled pid). | **Fixed** — the post-reap reaper now signals the surviving process GROUP only (`kill(-pid, SIGKILL)`, no `kill(pid)`). |
| R4 | P2 | Manifest type strictness is a behavior change for wrong-typed known fields (`min`/`max`/`timeout_seconds` were previously coerced/ignored). | **Accepted + documented** — `docs/plugins/manifest-v1.md` now states the exact type-check rule; silently coercing is the `as*()` hazard class #1038 exists to remove. Valid manifests are unaffected. |
| R5 | (self) | POSIX host could be killed by `SIGPIPE` when a write raced worker death (default disposition terminates the process). | **Fixed** — `IpcHandleStreamPosix::writeAll` blocks SIGPIPE for the write, consumes the raised signal, and restores the mask per-thread (no process-global `SIG_IGN` side effect). |
| R6 | (self) | Crash-respawn ladder: recovery could land on the last iteration and exit without executing against the fresh worker (a stale liveness observation consumed the retry), leaving a live worker behind a "crashed" error. | **Fixed** — the ladder is counted by PHASE (execute → blind retries until death is confirmed → recover once → execute → typed E6005); a recovered generation that dies fails immediately. Reproduced as an intermittent failure (1/10 full-suite runs), then 6/6 green. |
| R7 | (self) | `unloadPlugin` erased the map entry (destroying its `std::mutex`) while a `lock_guard` still referenced it — Debug-CRT abort `mutex destroyed while busy`, silent UB in Release. Pre-existing on master. | **Fixed** — the entry `shared_ptr` is copied and the mutex released before the erase. |
| R8 | (self) | 1024-deep JSON fixture overflowed the 1 MB debug stack in jsoncpp's recursive destructor. | **Fixed** — fixture depth 256 (still 64× the contract cap). |

Reviewer checks that came back clean (recorded so they are not re-litigated):
descriptor ownership on all spawn-failure paths; no double-join / self-join in
`IpcChannel`; POSIX liveness loop cannot spin to the timeout with a
descendant-held pipe and does not lose the buffered tail before the grace;
renderer recursion is bounded by both the contract and the renderer caps;
the #1040 response shape is consistent across worker/host/renderer; the
`unloadPlugin` fix takes `mMutex → entry->mutex` in the same order as
`diagnosticsSnapshot`/`invokeUi`.

## Traceability to the originating issues

| Issue | Status | Evidence |
|---|---|---|
| #1036 fd/handle leak | Resolved | single-owner streams + channel close ordering; `test_exprs_ipc` stream lifetime (steady-state delta == 0) and `test_plugin_host_process` spawn/shutdown/kill/respawn handle-count case. |
| #1038 unguarded `as*()` | Resolved in scope | `json_reader.h` + guards in manifest/package/workflow/index + worker registration report; 19-case wrong-type manifest suite, workflow hostile cases, non-object sbom install case. |
| #1039 hostile UI schema | Resolved | host revalidation in `describeUiSchema` (typed E5005) and renderer `attachPluginSchema`; total-control budget; depth/type caps; adversarial worker fixture (`deep`/`tree`/`flood`/`types`). |
| #1040 nested `ui.invoke` response | Resolved | `uiStateFromInvokeResponse`; real-worker runtime test on the production envelope; renderer widget test with the same shape; hostile state values ignored. |
| #1041 POSIX child-exit detection | Resolved | `waitpid(WNOHANG)` liveness + `postExitDrainGraceMs`; POSIX `[liveness]` tests (descendant-held pipe returns in ~0.5 s, no false `timedOut`; hung child still times out). |
| #1056 (plugin/IPC subset) | Not applicable / out of scope | the listed items are in `python_worker_provider.cpp`, `http_provider.cpp`, `tile_spec.h` — outside this track's owned files; no claim made. |

## Oracle evidence (final)

Windows (Debug, `-j1`, real worker + isolation + hostile fixtures), all seven
suites run twice consecutively — 2× green:
`test_plugin_host_process` (288/26), `test_plugin_ui_schema_host` (36/4),
`test_exprs_ipc` (117/23), `test_exprs_external_process_win` (46/8),
`test_plugin_manifest` (332/9), `test_exprs_workflow_schema` (23/6),
`test_plugin_ui_schema` (25/8). Full hostprocess suite additionally repeated
6× to clear the R6 flake.

POSIX (WSL, g++ -std=c++20, pinned Catch2), five suites run twice
consecutively — 2× green: `test_exprs_external_process` (49/11),
`test_exprs_ipc` (117/23), `test_plugin_manifest` (334/9),
`test_exprs_workflow_schema` (23/6), `test_exprs_plugin_system` (104/13).

Pre-existing, documented, not masked as green: the Catch2
`SIGTERM - Termination request signal` line emitted by the external-process
suite is a child-process signal-handler artifact (present on pristine
`origin/master`; the suite exits 0), and the hostprocess Debug-CRT teardown
abort (R7) was fixed because it blocked the whole suite on this host.

# Plugin Platform 8.0 — Adversarial Review Log

## Round 1 (pre-PR, mandatory): two read-only subagents over the full diff
`git diff origin/master...HEAD`, worktree feat/plugin-platform-8, 2026-09-11.
- Subagent A (architecture / correctness / concurrency / lifetime): 0 P0, 2 P1, 7 P2, 16 P3.
- Subagent B (tests / portability / bounds / docs-vs-code / conformance honesty): 0 P0, 3 P1, 5 P2, 11 P3.

## Dispositions

| ID | Finding | Severity | Disposition |
|---|---|---|---|
| A-P1-1 / B-P1-1 | `plugin test` executeBounded used `std::async`; future destructor BLOCKS on timeout — budgets illusory, wedged target hangs the CLI | P1 | FIXED: owned detached `std::thread` + shared result slot + timed poll; abandon on budget |
| A-P1-2 | Renderer multiline `setPlainText` re-emits `textChanged` → infinite host↔plugin event loop | P1 | FIXED: `QSignalBlocker` in every `applyValue` (all 6 control types) |
| B-P1-2 | host-process.md still claimed host-process+ui is "REFUSED" (contradicted code + own 1.1 section) | P1 (docs) | FIXED: both stale passages rewritten to the `access.ui=true` declarative route |
| B-P1-3 | declarative-ui.md described a production attach flow that does not exist | P1 (docs) | FIXED: "Lifecycle and integration status" section scopes what is wired (validator/worker/runtime/renderer/unload) vs the workbench-track shell seam; header comment fixed likewise |
| A-P2-1 | Malformed schema `version` killed the worker (jsoncpp LogicError on main thread) | P2 | FIXED: type-checked `isInt()` in the validator + try/catch around worker-side validation → typed E5005 |
| A-P2-2 | Unguarded jsoncpp casts on plugin-controlled JSON: validator `access.ui.asBool`, CLI `uiSchema.asBool`, package checksum `asString` | P2 | FIXED: all three type-checked before casting, manifest-diagnostic failures |
| A-P2-3 | Frame-cap negotiation lowered the WORKER READ cap: an oversized HOST request could tear the channel | P2 | WILL-NOT-FIX this round — analyzed: host requests are host-controlled (not plugin attack surface), the transport default bounds them, and the documented E6003 teardown remains the cap's enforcement for plugin responses. Follow-up noted: split per-direction caps. |
| A-P2-4 | `killProcess` not reentrant on Windows: loser closed handles the winner waited on | P2 | FIXED: only the liveness-exchange winner touches handles; POSIX loser still performs the idempotent group reap |
| A-P2-5 | Pool task exceptions `std::terminate`d the worker (modelRuntime tensor paths) | P2 | FIXED: workerLoop catch-all + executeModelRuntime typed failure |
| B-P2-8 | ui.invoke cancel-flag leak on the exception path | P2 | FIXED: scope-guarded unregister; flags registered at POST time (also A-P3-1: cancels while queued are no longer lost) |
| A-P2-6 | No FD_CLOEXEC on session pipes (fd leaks across concurrent workers; cross-channel write hazard) | P2 (pre-existing) | FIXED: host-side ends get FD_CLOEXEC at spawn |
| A-P2-7 | Child code after fork allocated (contradicting our own comment) | P2 | FIXED: exec strings precomputed before fork |
| B-P2-4 | Crash-path group reap was dead code (group id cleared before killProcess) | P2 | FIXED: id preserved for killProcess; fixture grandchild now closes inherited fds so the confirm-dead reap is the exercised path |
| B-P2-5 | Orphan test comment misdescribed the mechanism (grandchild held protocol fds → deadline ladder, not confirm-dead) | P2 | FIXED (with B-P2-4): fds closed in the fixture; comment corrected |
| B-P2-6 | PT_CANCEL matched "cancel" substrings and passed fast completions | P2 | FIXED: typed verdicts only (envelope `cancelled` flag / code 4000 / E6009); "not exercised" is its own distinct verdict |
| B-P2-7 | PT_CONCURRENCY message claimed "without corruption" but checked liveness only | P2 | FIXED: message now says liveness check, output equivalence not asserted |
| B-P2-9 / B-P3-9 | Skipped conformance checks counted as passes (a plugin declaring nothing looked 13/13) | P2 | FIXED: `summary.skipped` counted separately, excluded from `passed` |
| B-P3-10 / A-P3-2 | Poison test margins too tight for loaded hosts (matches the one recorded env-sensitive failure) | P3 | FIXED: ceiling 3 s / grace 1 s / B at [1.8, 4.4] s — margins ≥ 900 ms both sides; fixture gained an `ms` parameter |
| B-P3-11 / A-P3-11 | Empty checksum key = UB; junk digests accepted silently | P3 | FIXED: empty-key guard + 64-hex validation with explicit errors |
| B-P3-12 / A-P3-11 | `.staging` leftovers from crashed installs accumulate | P3 | FIXED: 24 h age-based sweep at install start |
| B-P3-13 | Single SHA-256 known-answer vector | P3 | FIXED: boundary-length vectors (1/2/55/56/63/64 bytes) pinned against coreutils sha256sum |
| B-P3-14 / A-P3-4 | Gate wait had an artificial 1 s floor exceeding short caller deadlines | P3 | FIXED: budget = the effective deadline |
| A-P3-3 | `mPoisoned` non-atomic formal data race | P3 | FIXED: `std::atomic<bool>` |
| A-P3-5 | Host shutdown (10 s) could race the worker's own 10 s drain | P3 | FIXED: host budget = drain + margin |
| A-P3-8 | `releasePluginUi` called the shell sink under the event mutex (re-entry deadlock risk) | P3 | FIXED: delegate cleared under lock, sink called outside |
| A-P3-9 | Renderer thread affinity unguarded | P3 | FIXED: attach refuses non-GUI-thread callers |
| A-P3-10 | Dead SpawnOptions restart fields implied session-level respawns | P3 | FIXED: removed (runtime owns the policy) |
| A-P3-15 | `hello()` returned a reference racing the reader thread | P3 | FIXED: copy under the mutex |
| B-P3-15 | setrlimit failure silent when the hard limit is lower | P3 | FIXED: parent-side getrlimit sanity → spawn warning diagnostic |
| B-P3-16 | capabilities.md overstated the workDir gate (opt-in + temp carve-out unclear) | P3 | FIXED: wording corrected |
| B-P3-17 | Orphan liveness probe counted zombies as alive | P3 | FIXED: /proc state check |
| B-P3-18 | Worker path broke on multi-config generators | P3 | FIXED: `$<TARGET_FILE:...>` |
| B-P3-19 | Wedged-drain worker returned through destroyed locals | P3 | FIXED: `_exit` after a failed drain |
| A-P3-7 | describePluginUiSchema held the host mutex across IPC | P3 | FIXED (narrow part): only the pointer fetch is locked; runtime-internal locking unchanged |
| A-P3-6 | ModelRuntimeCache single global mutex | P3 | ACCEPTED: one plugin per worker process — per-framework locks would not remove contention that matters |
| A-P3-13 | v1.0 host refuses v1.1 worker (E6001) | P3 | ACCEPTED: by-design fail-safe; host and worker ship from one SDK build |
| A-P3-14 | Pending-event queue bounds count, not bytes | P3 | ACCEPTED: queue only exists before the sink is installed (pre-plugin-code window); documented |
| A-P3-4 | Escalation grace holds the concurrency slot | P3 | ACCEPTED: bounded by killGraceMs; release ordering keeps slot accounting exact |
| A-P3-16 / B-P3-8 (int) | Renderer not yet wired into the app shell | note | DOCUMENTED (declarative-ui.md): shell integration is the workbench track's seam; renderer hardened this round |

## Post-remediation evidence
- Full sweep green: test_plugin_host_process 13 cases/111, ipc 13/68 (×4 incl. 1 transient
  load flake), capabilities 9/49, ui_schema 6/14, runtime_host 5/48, manifest 6/60,
  barrier 6/23, plugin_system 7/54, ui_schema_host 2/18 (offscreen), cli_commands_json 3/12.
- CLI `plugin test` on the isolation fixture: 13/13 checks, `summary {passed: 13, skipped: 0}`
  with strictly-typed PT_CANCEL/PT_CONCURRENCY verdicts.

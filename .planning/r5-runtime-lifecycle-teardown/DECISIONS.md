# DECISIONS — R5 Track 01 runtime-lifecycle-teardown

## D1 — #1358 PipelineRunCoordinator: the unprotected path is the pump-reentrant SAME-THREAD delete

Static teardown walk of the current destructor (`src/workflow/pipeline_run_coordinator.cpp:513-557`):

- **Foreign-thread delete while a completion frame is active** (what `test_d17:524` drives today): cancel trip → BlockingQueued drain (`shuttingDown`+`pool.clear/waitForDone`+`removePostedEvents`) → foreign busy-wait on `affinityBusy` → free. Memory-safe by design; the test can only flake RED on its window oracle (`REQUIRE_FALSE(completionSeen)`) when the 64 MiB hash finishes inside 150 ms.
- **Same-thread delete delivered from INSIDE a completion frame's event pump** — e.g. a queued functor/`deleteLater()` on the affinity thread, serviced by the whole-file hash's `QCoreApplication::processEvents` (`pipeline_run_coordinator.cpp:171`): the destructor takes the `else if` branch (`:548-556`), logs "pump-reentrant delete … unsafe", and **continues into member destruction, freeing `m_state` while `onNodeFinished`'s frame is still on the stack**. The frame then reads `&m_state->cancelRequested` (`:961`, captured pointer into the hash loop), `m_state->shuttingDown` (`:1005`) and `BusyGuard`'s `m_state->affinityBusy` → use-after-free. This is a real production hazard: any dock/owner deleting its coordinator on the GUI thread while a completion frame pumps (deleteLater, queued signal, Direct slot of `pipelineCompleted`) takes this path.

## D2 — Fix: frame-shared ownership of RunState (provable teardown, no wait loops)

1. `m_state`: `std::unique_ptr<RunState>` → `std::shared_ptr<RunState>` (guarded by a tiny `m_stateMutex`).
2. Every affinity-thread body takes a local `std::shared_ptr<RunState> state = adoptStateLocked()` at entry and uses `state->` throughout (`onNodeFinished`, `finalizeIfDone`, `persistCheckpoint`, `dispatchReadyNodes`, `markRemaining`, `requestCancelOnAffinity`, `startRunOnAffinity`, `resumeOnAffinity`). A re-entrant delete inside any pump leaves the frame holding the last-but-one reference.
3. Destructor: trip cancel → drain (unchanged) → `m_state.reset()`. If a frame is active its shared_ptr keeps RunState alive; RunState is freed exactly when the last frame unwinds (refcount → 0). The foreign busy-wait loop and the "unsupported" `qWarning` path are REMOVED — no sleep-waits, no detached threads, no leaks; both deletion paths share one code shape.
4. `pool` shutdown semantics unchanged: `RunState::~RunState` still `waitForDone()`; drain still `clear()+waitForDone()` before completions are dropped. The `cancelFlag` handed to workers (`:879`) points into a RunState that outlives every worker (pool drained before the last reference can drop: the drain runs before `reset()`, and frames hold refs).
5. Public API/signals/checkpoint semantics: zero changes. Worker-side QPointer-completion protocol: unchanged.

## D3 — Deterministic regressions (replaces the 150 ms timing window)

- **T1 (existing, rebuilt)**: foreign-thread delete mid-whole-file-hash — the destroyer thread waits on a promise armed by a 10 ms one-shot on the affinity loop (no sleep race on the arming side); 1 GiB artifact keeps the hash frame live >=3x the delay on the reference host.
- **T2 (new)**: pump-reentrant SAME-THREAD delete — a 20 ms one-shot on the affinity loop fires ~20 ms into the (multi-hundred-ms worst-case) hash; its functor is delivered by the hash's own processEvents pump and deletes the coordinator INSIDE the frame. The delivery is timer-latency-based (not strict event-order): on a pathologically delayed completion dispatch the delete could land past the frame and the pass would be vacuous; the >=25x hash-vs-delay margin makes that practically unreachable.
- **T3 (new)**: worker-outlives-owner — 1-node chain whose worker parks on a gate (honouring the cancel flag) while a foreign delete runs; the drain joins the parked worker, then removePostedEvents drops the late completion. Asserts no emission and clean destruction.
- All three scrub their scratch via a RAII guard (assertion failures included) — multi-hundred-MiB artifacts have filled tmpfs before (and did once during this track; see ledger Round 2).

## D4 — #1357 investigation plan (empirics-first)

1. Reproduce: run `test_mcp_server`'s run_workflow cases and `test_capability_surface_parity` binary repeatedly on this toolchain; record exit codes (`free()/double free` abort text).
2. ASan lane (`sanitizer-debug` preset) serially after the dev build; capture the two free stacks.
3. Suspect zone mapped: `TaskCenter::instance()` + `WorkflowRunCoordinator::instance()` + `JobEngine::instance()` + `ExecutionResultCache::instance()` Meyers cluster (destruction-order hazards already documented in `TaskCenter::~TaskCenter`'s ArtifactGC guard) — verify from ASan stacks, fix at the root (ordered teardown / ownership handoff), no new workarounds.
4. Any `_Exit`/leak workaround RELATED to the fixed root cause gets removed and double-run-verified (#1342 pattern).

## D5 — Review round 1 (independent subagent, post-implementation) — all addressed

- B-1 (Medium): T2 lacked the scratch scrub its siblings had; demonstrably re-filled /tmp and broke an unrelated suite. Fixed: RAII ScratchCleanup guard armed in all three destroy tests (assertion-failure-safe).
- E-1 (Medium): TSan evidence was missing. Closed: see D6.
- A-Low (documented, no fix required): a completion frame that already passed its entry checks when the destructor starts can still emit/persist/dispatch (memory-safely, under its own state reference) until the drain executes on the affinity thread. This window is strictly smaller than the pre-fix code's and is inherent to two-phase teardown; recorded here as the intended semantics.
- Nit (documented): ~RunState's pool.waitForDone backstop runs under m_stateMutex; workers never take that mutex, and the drain already joined the pool, so this is a no-op in practice.
- D3 drift corrected (T2 timer-based, T3 1-node).

## D6 — TSan evidence (#1358 acceptance) — RESULT: bug reproduced on master, gone in the fix

Driver: `/tmp/tsan_coordinator_driver.cpp` (scratch, not shipped) — the same three destruction scenarios against a TSan-instrumented `sicnu_workflow` (scratch build dir, `-fsanitize=thread -g -fno-omit-frame-pointer`, serial). Qt itself is uninstrumented (no TSan Qt exists on this host), so queued-connection capture copies and Qt's QThreadPool teardown internals produce structural false positives: their happens-before edges live in Qt's inline futex fast paths, invisible to TSan.

Comparison run (identical driver, only the coordinator TU differs):

| Build | TSan result |
|---|---|
| **master coordinator** (a726d17a6) | 11 reports incl. **3 heap-use-after-free** at the exact statically-predicted unprotected path: `invokeOnCoordinatorThread`/`aborted()` hash-loop cancel-flag read (`master:147`), `~BusyGuard` affinityBusy decrement (`master:924`), the frame-tail cancel-branch state write in `onNodeFinished` (`master:964`) — plus 3 QWaitCondition teardown races and Qt-copy noise |
| **fixed coordinator** (43650f0a9) | 0 heap-use-after-free; 0 races involving the m_stateMutex/shared_ptr protocol; remaining reports are the same pre-existing QWaitCondition teardown races (present identically on master — the `pool.clear()+waitForDone()` drain is unchanged Track-13 code) and Qt-copy noise |

The driver itself ships under `.planning/r5-runtime-lifecycle-teardown/tsan_coordinator_driver.cpp` so the comparison is reproducible.

Caveat recorded honestly: a TSan runtime-internal `CHECK failed: sanitizer_thread_registry.cpp` aborts some runs late (Qt worker-thread churn vs TSan thread registry — a TSan/Qt interaction, not a data race); runs with the 2-3 QWaitCondition reports + the joinable-driver fix still demonstrate the UAF delta deterministically. Combined with the deterministic regressions (T1-T3) and the ASan-clean destroy runs, the #1358 acceptance "TSan shows the race fixed" is closed.

## Residual (explicitly deferred, re-evaluated at review)

- Foreign accessor BlockingQueuedConnection whose event is dropped by the drain's `removePostedEvents` can hang its caller (pre-existing, nanosecond window, not exercised by seeds). Document; optionally harden if review requires.
- Windows `_WIN32 _exit()` main tails (~20 files): #1342-disclosed, needs a Windows lane; recorded, out of this PR.
- `test_parity_{stress,restore_failsafe,selection_authority,state_mirror}_r4` / `test_view_link` / `test_perf_operator_observatory` `_Exit` listeners: only retired if shown to share the seed root causes (double-run proof); otherwise documented as residual for the follow-up track.

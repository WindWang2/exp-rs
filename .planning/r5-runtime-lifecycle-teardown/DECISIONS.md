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

- **T1 (existing, kept)**: foreign-thread delete mid-whole-file-hash.
- **T2 (new, deterministic)**: pump-reentrant same-thread delete — the executor posts the completion and a queued "destroyer" functor to an affinity-thread helper QObject; event order (completion, destroyer) guarantees the delete lands inside the hash's own pump. Asserts: no crash, `guard.isNull()`, `nodeFinished` abandoned (no emission after destruction), run state never mutated post-drain.
- **T3 (new)**: worker-outlives-owner — 2-node chain, worker parked on a gate while a foreign delete runs (drain passes with the worker parked → `pool.waitForDone` returns only after the gate releases); the late completion must never reach a dying object; asserts no emission and clean destruction.

## D4 — #1357 investigation plan (empirics-first)

1. Reproduce: run `test_mcp_server`'s run_workflow cases and `test_capability_surface_parity` binary repeatedly on this toolchain; record exit codes (`free()/double free` abort text).
2. ASan lane (`sanitizer-debug` preset) serially after the dev build; capture the two free stacks.
3. Suspect zone mapped: `TaskCenter::instance()` + `WorkflowRunCoordinator::instance()` + `JobEngine::instance()` + `ExecutionResultCache::instance()` Meyers cluster (destruction-order hazards already documented in `TaskCenter::~TaskCenter`'s ArtifactGC guard) — verify from ASan stacks, fix at the root (ordered teardown / ownership handoff), no new workarounds.
4. Any `_Exit`/leak workaround RELATED to the fixed root cause gets removed and double-run-verified (#1342 pattern).

## Residual (explicitly deferred, re-evaluated at review)

- Foreign accessor BlockingQueuedConnection whose event is dropped by the drain's `removePostedEvents` can hang its caller (pre-existing, nanosecond window, not exercised by seeds). Document; optionally harden if review requires.
- Windows `_WIN32 _exit()` main tails (~20 files): #1342-disclosed, needs a Windows lane; recorded, out of this PR.
- `test_parity_{stress,restore_failsafe,selection_authority,state_mirror}_r4` / `test_view_link` / `test_perf_operator_observatory` `_Exit` listeners: only retired if shown to share the seed root causes (double-run proof); otherwise documented as residual for the follow-up track.

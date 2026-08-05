# ADR 0051: TaskCenter Owns the JobEngine Listener

## Status
Accepted

## Context
`TaskCenter::watchSubmittedJob` spawned a detached `std::thread` per job,
polling `JobEngine::snapshot()` every 5ms to re-implement forwarding the
engine's own listener already provides — a watcher alive at process exit
caused the flaky ctest #23 SEGFAULT. The dead `JobEngineQtBridge` held the
single listener slot with zero connections; `JobEngine::cancel` invoked
CancelHooks while holding `m_mutex` (re-entrancy deadlock).

## Decision
1. **TaskCenter installs one JobEngine listener** (`onJobRecord`) replacing
   `watchSubmittedJob`; watcher threads deleted. Dedup/delta logic (log
   dedup keyed on `logLines.size()`, progress change, single terminal
   transition) is ported with identical observable behavior.
2. **Listener re-installed on every submit** (`setListener` is a single
   replacing slot; a test-side reset cannot detach bookkeeping); a post-
   submit snapshot catch-up covers jobs finishing before the jobId→taskId
   mapping is registered; terminal `mark*` methods treat terminal as final.
   The destructor joins the engine's workers (`shutdown()`) so an in-flight
   job can never notify destroyed state.
3. **Delete `JobEngineQtBridge`** (files, app/test CMake entries,
   instantiation in `main_window_docks.cpp`, stale includes/comments);
   TaskCenter is the listener's sole production owner.
4. **`JobEngine::cancel` invokes the CancelHook after releasing `m_mutex`**
   (mirroring `notify()`); cancels landing in the worker's pick-vs-arm
   window arm a pre-set flag the worker adopts.

## Consequences
- **Process-exit SEGFAULT race eliminated** — no detached threads outlive
  TaskCenter state; a single forwarding path replaces poll-plus-listener.
- **Cancel hooks deadlock-free by contract**; Running cancels cannot miss
  the flag window.
- **Tests installing their own listener** must respect the single slot; TaskCenter re-claims it on its next submit (ADR 0052 adds pruning).
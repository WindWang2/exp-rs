# Plugin Platform 8.0 — Performance / Resource Evidence

Boundedness (the primary performance contract for isolation boundaries):

- Concurrency ceiling: worker dispatch width ≤ 8 (kWorkerMaxSlots) AND ≤ quota.maxRequestConcurrency
  (host gate). Host request slots: exactly maxRequestConcurrency; bounded FIFO wait = the effective
  deadline; overflow → typed E6007, no unbounded queueing.
- Per-request time ceiling: min(caller deadline, requestDeadlineMs quota); escalation grace ≤
  killGraceMs (default 3 s) then force; a poisoned worker is killed at drain. Worst case per
  request: one deadline + one grace — never unbounded.
- Frames: symmetric cap (default 32 MiB, negotiated downward to maxResponseBytes); progress frames
  coalesced per request to ≥ 20 ms windows; host pending-event queue ≤ 1024 (drop-oldest + counter).
- Delivery of rendered UI events: single serialized thread, queue ≤ 64 (drop-oldest + counter);
  GUI thread never blocks on IPC.
- Packaging: SHA-256 streamed in 256 KiB chunks (O(bytes), constant memory); staging is
  same-filesystem rename — no cross-device copies.

Measurements (local, load ~11 host):

- Worker round-trip (echo operator): unchanged from baseline — the concurrency gate adds a
  mutex/CV check in the nanoseconds range per request; no measurable regression in the suites
  (suite wall time dominated by process spawn + protocol, as before).
- CLI conformance run end-to-end: < 60 s including ~4 worker spawns, a crash-recovery cycle,
  a cooperative cancel, 3 parallel executions, schema describe/invoke (see TEST_MATRIX).
- test_plugin_host_process full suite: ~90 s wall (13 cases; largest are the escalation tests
  with deliberate timeouts). All timing budgets in tests are explicit constants.

No benchmark regressions claimed beyond these bounds; hot-path changes are limited to one
mutex/CV gate per request and an atomic load per frame write/read.

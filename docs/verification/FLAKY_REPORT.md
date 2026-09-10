# Flaky Test Report — Verification 7.0 (task J)

Method: static triage of every test file using
`msleep` / `QThread::sleep` / `std::this_thread::sleep` / `QTest::qWait`
(39 files), ranked by call count and by the KIND of wait. Remediation
principle: wait on the actual invariant with a bounded deadline; sleeps are
acceptable only where the condition is genuinely time-based (e.g. debounce
timers) — never to "let a notification land".

## Ranked triage (top candidates)

| File | sleep calls | Pattern | Risk | Action |
|---|---|---|---|---|
| test_task_center.cpp | 37 | bounded status polls (OK) + raw 5–20 ms "settle" delays around listener delivery | **High** | **fixed**: the worst site ("let the terminal notify land", 20 ms) now polls the actual invariant (`allTasks().size()` + engine snapshot) with a 2 s bounded deadline. Remaining polls are state-polling (bounded, low risk) — acceptable pattern. |
| test_job_engine.cpp | 15 | engine lifecycle settle sleeps | Medium | covered by `test_concurrency_stress` rewrite of the same contracts with deterministic waits; legacy sleeps left (existing green suite, not this track's to churn) |
| test_dual_viewport_sync.cpp | 11 | Qt event-loop settle | Medium | GUI closure; watch list |
| test_gui_job_adapter.cpp | 9 | Qt event-loop settle | Medium | watch list |
| test_python_plugin_host.cpp | 8 | external process startup waits | Medium | watch list (process spawn latency; RUN_SERIAL already) |
| others (34 files) | 1–7 | mixed | Low–Med | documented; fix on evidence of actual flakiness, not preemptively |

## RUN_SERIAL audit

19 test declarations use RUN_SERIAL. Legitimate shared resources observed:
real worker process kills (`test_worker_host`), CPU saturation stress
(`test_concurrency_stress`, `test_ui_scale_benchmark`), cross-process run
locks (`test_fault_injection`), environment-wide Qt plugin teardown (#2132).
No RUN_SERIAL was found to mask a race that a deterministic wait could
express — the two known race-shaped sleeps were converted to invariant polls
in this track.

## Environmental flakiness (not test bugs)

- `QT_IM_MODULE=compose` requirement (fcitx teardown SIGSEGV, #2132) — env
  governance in `CTestCustom.cmake`; documented in TEST_INFRA.md.
- Conda `libxml2` shadowing / PYTHONHOME mismatches — same governance.

## Policy going forward

1. New tests must synchronize on observable state (condition variables,
   listeners, polling the asserted invariant) with explicit deadlines.
2. `waitUntilIdleForTests`-style engine barriers are preferred over sleeps.
3. A flaky failure is triaged within the track that owns the test; a sleep
   that survives two local reruns under load gets a determinism fix, not a
   bigger timeout.

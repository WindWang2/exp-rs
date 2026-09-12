# Plugin Platform 9.0 — Performance & resource bounds

Environment: Linux x86_64, 6.18.49-2-lts, GCC C++20, **Debug build** (all
numbers below are Debug; no Release numbers were taken on this host and no
Debug-vs-Release comparison is implied), ninja -j4 under heavy co-tenancy
(other 9.0 track builds were running concurrently; load average 16–23 during
measurements — numbers are therefore UPPER bounds, not best-case).

## Measured

| Item | Result | Notes |
|---|---|---|
| `plugin index` scan, 100 manifest-only packages | ~0.6 s total | WHOLE CLI process wall time (binary startup + Qt init dominate); scan itself is one `plugin.json` parse per directory, O(N) files, O(1) memory per entry, output sorted |
| Full plugin regression (11 suites, 836 assertions) | passes with co-tenant load 16–23 | includes dozens of real worker spawns, kill ladders, poison-drain cycles and 3×7-caller stress rounds |
| Conformance kit run (19 checks, all 9.0 targets declared, incl. PT_QUOTA deadline waits) | completes inside its own ~45 s budget envelope | dominated by deliberately-waited deadlines (PT_CANCEL 300 ms window, PT_QUOTA 3 s manifest deadline), i.e. by test DESIGN, not host overhead |

## Analysis-based bounds (stated, not measured)

- Per-direction frame caps add two atomic loads per frame (one per
  direction) versus one before — no algorithmic change, same O(1) framing
  hot path.
- `validateUiEvent` is O(value serialization) bounded by
  `maxEventValueBytes` (4096 B) and O(1) otherwise; it runs once per
  ui.invoke, before any transport work.
- `redactSecrets` is O(nodes × key length); the debug bundle is a support
  path, not a hot path.
- `versionSatisfiesRange` is O(1) string parsing per call;
  `reportDependencyStatus` is O(dependencies × installed) with one manifest
  read per dependency candidate — bounded by the installed set, install-time
  only.
- ConcurrencyGate stats (waiting/peak) are reads under the existing gate
  mutex — no new synchronization on the request path.

## Resource bounds carried from 8.0 (unchanged)

Bounded event queue (1024, drop-counted), progress coalescing (≥ 20 ms),
FIFO concurrency gate with typed overload refusal, per-direction frame caps
(monotonic), worker memory cap (Windows job object / POSIX RLIMIT_AS),
restart policy (3 / 60 s), staging sweep (24 h), bounded drain windows.

## Explicitly not measured (and why)

- Worker RSS under load: no isolation on this co-tenanted host (load 16+
  from sibling tracks); a number taken here would be noise. Marked not-run.
- Release-build throughput of the channel: the Debug lane is the only
  locally reproducible lane on this host. Marked not-run; any Debug-vs-
  Release comparison would be misleading and is deliberately absent.

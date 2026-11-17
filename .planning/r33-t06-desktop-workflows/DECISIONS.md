# R33-T06 — DECISIONS

Unattended mode: options and the taken default are recorded here instead of
asked. Every entry is reversible unless marked otherwise.

## D-01 — Preview identity is captured once, at request time
**Options.** (a) Re-stat at completion (status quo — poisons the cache when the
file changed); (b) capture the key at request and use it for the cache write;
(c) hash the file contents.
**Taken: (b).** (c) is refused by the brief ("不为预览全文件 hash 巨型影像") and
would cost a full read per thumbnail. (a) is the defect being fixed.

## D-02 — A render whose identity moved is delivered, not discarded
**Options.** (a) Drop it and show nothing; (b) deliver it flagged
`identityChanged=true` and cache nothing; (c) bounded re-dispatch of the new
generation.
**Taken: (b).** (a) leaves the detail pane blank for a file that plainly exists —
a worse UX than an honestly-flagged older generation. (c) doubles the read on a
rare path and needs retry state; the very next user interaction re-requests
anyway (and now always misses the cache). The flag is additive API, so no
consumer breaks.

## D-03 — ±Inf is a display miss, not a clamped endpoint
**Options.** (a) Clamp ±Inf into the range endpoints; (b) treat as NoData/black;
(c) drop only from the range, still scale.
**Taken: (b).** (a) fabricates a data extreme the file never claimed and keeps
`t` non-finite; (c) leaves the pixel colour undefined. Black matches the
existing NoData/NaN display policy, and the raster itself is never modified.

## D-04 — `geo::bandSentinelMatches` is the NoData authority here too
The preview used a raw `v == band.noDataValue` while `raster_reader.h` declares
`bandSentinelMatches` "the one NoData comparison authority" (Float32 narrowing,
NaN-exact). Switched to it — same observable behaviour for Float64, correct for
Float32 quantisation, one less local rule.

## D-05 — Abort at supersede/cancel time, not at delivery time
**Options.** (a) Keep delivery-time-only dropping (status quo: 1000 clicks queue
1000 real raster reads); (b) shared atomic abort flag checked before the worker
touches pixels.
**Taken: (b).** It cannot stop a render already inside GDAL (bounded by pool
concurrency, 2), which is the honest limit; everything still queued costs one
flag read.

## D-06 — `m_canceled` retired
With D-05 the pending registry itself is the cancellation record: `cancel()`
removes the token, so `onComputed` cannot find it. Keeping both mechanisms would
have meant two sources of truth plus the 1024-entry pruning hack. Removal is
orphan cleanup of state this change made dead.

## D-07 — `pendingRequests()` is public
The bound is now a contract, so it needs an observable. Used by the WP04 test on
every iteration of a 1000-request burst; also useful in diagnostics. Returns the
registry size, nothing more.

## D-08 — No test-only hook in the service
The A→B barrier is built from a single-thread `QThreadPool` plus a fence task
(the fence can only run after the preview task returns), so the test controls
the ordering exactly without any production seam.

## D-09 — i18n: fix the classifier false positive, do not move the baseline
`hard_en` came from a **pure format template** (`"%1 — %2"`: one placeholder,
separator only, no words). `looks_like_text()` now excludes that shape
(`is_pure_format_template` requires a placeholder AND a letter/Han-free
residue), so `"%1 of %2"` stays `hard_en`. Baseline, ceilings and the scan list
are untouched. The three per-file `nonui_zh` cap overruns are backend text owned
by T04 and stay red (see HANDOFF).

## D-10 — OpenCV is an environment parameter, not a repo change
Configure requires `-DOpenCV_DIR=<shared prefix>`; the prefix lives in the
previous session's `.scratch`. Passed on the command line only — no CMakeLists
edit, no dependency pinning.

## D-11 — Build/test resources
User-mandated cap `-j4` (lower allowed) through the double `resource_guard`;
`CTEST_PARALLEL_LEVEL=1`; the repository's own default guard is left at 2.

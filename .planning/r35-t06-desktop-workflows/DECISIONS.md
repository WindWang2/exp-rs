# R35-T06 — DECISIONS

Unattended mode: options and the taken default are recorded here.

## D-01 — Continue in the R33 track's worktree instead of a fresh checkout
**Options.** (a) `git worktree add` a new directory as the brief scripts it;
(b) abort the track; (c) switch the previous R33 worktree (clean, its PR merged)
to the new `zcode/r35-…` branch and reuse its build directory.
**Taken: (c)**, after two aborted checkouts.
**Evidence.** The brief's script ran twice: 9.8 MB then 140 MB of 16 385 files
at ~9 files/s before the host killed it (`load average 6–9`, five sibling tracks
building on the same NVMe). A fresh checkout was ~40 min and not resumable; a
fresh configure+build afterwards is another 3–5 h at `-j4`. The reused worktree
is **this track's own**, clean (`git status` empty), and its branch is merged, so
nothing of value is at risk.
**Why the evidence is still valid.** `make` decides by content and timestamps,
not by build-dir identity: after switching to `120677e6` every source that
differs is newer than its object and is rebuilt. The measured delta from the R33
tip is 155 files, of which `src/core` **0** and `src/gui` **0** — so the QGIS
objects are provably still valid for this source.
**What is not preserved.** The worktree directory keeps its `r33` name; RESUME.md
records the real paths. This is cosmetic, not a correctness gap.

## D-02 — A dead receiver or a destroyed service aborts the queued read
The R33 fix gated *delivery* on the receiver `QPointer` and on `!self`, but the
worker still entered the renderer and read the whole file before noticing. R35
sets the same shared abort flag that `cancel()`/`supersede` use: the
`destroyed()` handler aborts the receiver's token, and the dtor flags every
in-flight request. A render already inside GDAL remains uninterruptible — that
bound is the pool's concurrency, and it is stated in the header rather than
pretended away.

## D-03 — `rendersStarted()` as the real-work counter
`pendingRequests()` counts the registry, which is what made the destruction gap
invisible: after `delete receiver` the registry drops to 0 while a read is still
queued. A monotonic per-service counter of *entered renders* is the observable
the work package needs (and WP03 asks for "实际读取次数" explicitly). It is
diagnostic only — nothing schedules on it — and it intentionally counts a render
whose result is then discarded, because that is what "real work happened" means.

## D-04 — Translation freshness: fix the one real gap, alias the four
extraction artifacts
**Measured at `120677e6`: `FRESHNESS GATE: FAIL (5)`.** All five are vendored
QGIS strings; the split is not uniform and matters:
* `QgsFieldMappingModel / "Field Mapping"` is a **genuine** gap. The tracked
  entry sits under `QgsFieldMappingModel::ExpressionContextGenerator`, which is
  in the gate's stale list (lupdate no longer emits it). The call at
  `src/gui/qgsfieldmappingmodel.cpp:653` is inside the nested
  `ExpressionContextGenerator`, which declares no `tr` of its own, so C++ name
  lookup finds the enclosing `QgsFieldMappingModel` QObject's `tr`. Runtime
  context = `QgsFieldMappingModel`. Added there with a zh translation.
* `QgsAbstractContentCache` (2 messages) and `QgsProjectStoredObjectManagerModel`
  (2 messages) are **lupdate extraction aliases**. The code is a class template
  (`template<class T> class QgsAbstractContentCache : public
  QgsAbstractContentCacheBase`, `src/core/qgsabstractcontentcache.h:236`) with no
  `Q_DECLARE_TR_FUNCTIONS`, so runtime lookup goes through the inherited
  `QgsAbstractContentCacheBase::tr` — the context the file already carries. The
  repository's own convention for exactly this situation is an alias context with
  an `extracomment` (the file already had `T` and `*Base` for it); lupdate has
  since changed the name it emits, so the alias set is updated to match.
**Result: `FRESHNESS GATE: PASS (unfinished=0<=0, new messages=0)`**, 24 029
translations compiled by `lrelease` with 0 unfinished.

**Durability caveat (review R1 finding 1).** The `extracomment` text on the alias
entries is *one-shot*: the reviewer ran the project's own `lupdate` invocation on
a copy and the comments were dropped while the translations survived. The durable
record of "the runtime context is the `*Base` class" is therefore **this file**,
not the `.ts`. Re-running `make sicnu_i18n_update` will not break the gate (the
reviewer re-ran it afterwards: still PASS), it only drops the explanation.
R1 also confirmed the alias set does not mask anything: the gate independently
requires the `*Base` keys, so a missing runtime translation would still fail.

## D-05 — No baseline, ceiling or scan-list change
`tests/fixtures/i18n_string_baseline.json` is untouched, as is the audited file
list. The canonical gate's numbers are not "fixed" by moving a threshold.

## D-06 — Scope this session to preview lifecycle + translation freshness
WP06–WP18 need `sicnu_geo_rs_shell` (the cartography dock test links the whole
shell), i.e. another multi-hour build on a saturated host. Shipping two verified
work packages beats shipping eight unverified edits; the residuals stay in
HANDOFF with the file:line evidence the R33 ledger already established.

## D-07 — Two independent test binaries, one appended CMake block
`test_r35_t06_preview_lifecycle` compiles the service directly (same pattern as
`test_asset_preview_service`), so it does not pull the application. The
`tests/CMakeLists.txt` edit is a pure EOF append; no other track's block is
reordered.

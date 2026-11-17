# R33-T06 — preview generation identity, non-finite display rule, bounded pending work, i18n classifier precision

## Problem

Three defects in the lazy-preview path made a thumbnail answer for the wrong
file, and one audit rule misclassified a format template as untranslated
English.

1. **A file replaced during the render poisoned the cache with the old pixels.**
   `AssetPreviewService` derived the cache key twice: once at request time to
   look the entry up, and again in `onComputed()` to insert it. If the file was
   rewritten while the worker was reading, generation A's pixels were stored
   under generation B's key, and every later request for B was answered from the
   cache with A's image — permanently, not once.

2. **One infinite sample blanked the whole preview.** `computeRange()` skipped
   NaN and NoData but accepted ±Inf, so `max` became `+Inf`; every finite pixel
   then mapped to `t ≈ 0`, and the pixel that was itself infinite produced
   `t = NaN`, which `static_cast<quint8>` converts under **undefined behaviour**.
   A legitimate Float64 span such as `{-1e308, +1e308}` failed the same way: the
   denominator `max - min` overflows to `+Inf`.

3. **Every click did real work.** Superseding or canceling a request only
   dropped the result at delivery time; the raster read had already been queued
   and executed. `m_inFlight` also grew with click rate.

4. **`hard_en = 1` on the canonical i18n gate** came from
   `data_manager_panel.cpp:1002`, `QStringLiteral("%1 — %2")` — one placeholder
   plus a dash. There is no English word in it to translate (issue #2108
   reproduces the gate; this row is its `hard_en` part).

## What changed

* **Identity captured once** (`src/app/preview/asset_preview_service.{h,cpp}`):
  `InFlight::cacheKey` is computed at request time and used for lookup *and*
  insertion; the render is only cached while that identity still describes the
  file. `Result::identityChanged` reports the mismatch, and the Data Manager
  detail pane re-reads exactly once (bounded by a `retried` flag) instead of
  keeping the old generation on screen. No new `tr()` source was added, so
  `check_translation_freshness.py` keeps its inherited 5 failures and gains none.
* **Display rule** (§2): non-finite samples are display misses — like NoData,
  black, and never a range input. `scaleToByte()` refuses a non-finite value or
  ratio outright, and the stretch uses a half-range form so a `2e308` span keeps
  a finite denominator. NoData comparison now goes through
  `geo::bandSentinelMatches`, the header's single NoData authority (Float32
  narrowing, NaN-exact).
* **Bounded pending work** (§3): each request owns an abort flag shared with its
  worker; supersede and `cancel()` set it and remove the token immediately, so
  a queued worker returns before touching pixels. `m_canceled` became dead and
  was retired. `pendingRequests()` exposes the registry size (documented as
  *not* a running-render count).
* **i18n precision** (`scripts/audit_i18n_strings.py`): `is_pure_format_template()`
  exempts a literal only when it carries a placeholder and the residue contains
  no letter in **any** script. `"%1 of %2"`, `"3 pages"` and `"Всего %1"` stay
  `hard_en`. Baseline, ceilings and the scan list are untouched.

## Boundaries

* Not touched: `resources/translations/sicnu_zh_CN.ts`,
  `scripts/stage_windows_application.ps1` (#2134-exclusive);
  `tests/test_bilingual_delivery_ui.cpp` (T05);
  `src/agent/cartography/cartography_operators.cpp` and its tests (T04);
  `src/data/execution_fingerprint.*`, `src/platform/portable.h`,
  `src/geospatial/util/atomic_fs.*` (T03/T04).
* `tests/CMakeLists.txt` gains **one appended target block** at EOF; no
  existing block is edited, reordered or reformatted (three open PRs touch this
  file).
* `.gitignore` gains the usual markdown-only exception for this track's
  planning directory (appended block).
* The cache identity remains `path + size + mtime + target size`. A rewrite that
  preserves both size and mtime is still indistinguishable — no content hash is
  added, by design (hashing a multi-GB raster per thumbnail is what this service
  exists to avoid). The header now says so.

## Verification

| Command | Result |
|---|---|
| `python3 scripts/audit_i18n_strings.py --files tests/fixtures/i18n_audit_files.txt --gate tests/fixtures/i18n_string_baseline.json --selftest` | `SELFTEST PASS`; `hard_en` **1 → 0**; `nonui_zh=1095 > 1059` and the three backend per-file caps **still fail** — inherited, owned by T04 (see the planning HANDOFF). |
| `cmake --preset dev-default -DOpenCV_DIR=…` (Debug, tests ON) | see ACCEPTANCE.md |
| `cmake --build build-dev --target test_r33_t06_preview_identity test_asset_preview_service` | see ACCEPTANCE.md |
| `ctest -R 'r33_t06_preview|asset_preview' -j1` | see ACCEPTANCE.md |

New tests (`tests/test_r33_t06_preview_identity.cpp`) use real GDAL rasters in a
`QTemporaryDir`; the A→B barrier is a single-thread pool plus a fence task, so no
timing is involved and no test-only hook was added to the service. Cases: replaced
file mid-render, unchanged file still cached, Unicode path + deletion, Float32
special values, Float64 −Inf, `2e308` span, Float32 non-exact sentinel, 3-band
+Inf, all-missing, flat, per-band NoData, 1000-request burst, cancel, and
destruction with work in flight.

## Review

R1 by an independent reviewer (no part in the implementation, read the actual
`BASE..HEAD` diff): contracts 2–4 SOUND, contract 1 CONCERN — `identityChanged`
had no consumer and the panel kept showing the old generation. All twelve
findings are dispositioned in `.planning/r33-t06-desktop-workflows/REVIEW_R1.md`;
the P2 (panel consumption) and the overflow/identity/coverage findings were fixed
and re-reviewed in R2.

## Tracking

Refs #2108 (the remaining gate failures are the three backend files, T04's).

## Honest limits

* The five E2E chains in the track brief are **not** all covered here: WP06–WP18
  are `NOT_EXECUTED` with file:line handover evidence in `PLAN.md` /
  `HANDOFF.md` (cartography draft-vs-compiled, multi-page export, history
  truncation label, Studio curve truth).
* No native desktop/DPI/dark-mode acceptance run (offscreen only).
* `test_data_manager_panel` was not built, so the panel caption change is
  compile-verified only; H-08 hands over the file-watching/re-request follow-up.

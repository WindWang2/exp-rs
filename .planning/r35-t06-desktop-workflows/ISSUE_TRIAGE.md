# R35-T06 — ISSUE TRIAGE

Vocabulary: `NEEDS_REPRO / CONFIRMED_STATIC / REPRODUCED / ACTIVE_PR_COVERED /
FIXED_OPEN / PARTIAL / SUPERSEDED / OUT_OF_SCOPE`. Open issues: 106 (API count,
PRs filtered). Labels and aggregate counts are not dynamic evidence.

## Touched by this session

| Issue | Class | Evidence / owner |
|---|---|---|
| **#2081** i18n: freshness gate fails on master (5 FAIL) + `test_bilingual_delivery_ui` asserts English | **PARTIAL → FIXED for the freshness half** | Re-measured at `120677e6`: `FRESHNESS GATE: FAIL (5)`. After this change: `PASS (unfinished=0<=0, new messages=0)`, 24 029 translations, `lrelease` 0 unfinished. The `test_bilingual_delivery_ui` half is **T05's test** and was not run; the issue must not be closed on this evidence alone. |
| **#1695** [R17-ui][P3] ui-lane cleanup index | **PARTIAL** | Its "dead-code lifecycle defects" shape is exactly what WP02 closed for the preview service (a destructor that dropped delivery but not work). The rest of the index was not swept. |

## Relevant but not this track's to fix

| Issue | Class | Note |
|---|---|---|
| **#2108** i18n canonical gate | SUPERSEDED | The canonical gate is green at this HEAD (`SELFTEST PASS`, `GATE PASS`) — the R33 classifier fix plus the merged backend work. Nothing to do here; kept as regression. |
| **#2082** 5 zh_CN contexts keyed by short class name | **CONFIRMED_STATIC, owner = global TS (T06) but not this session** | Same family as the alias-context analysis in DECISIONS D-04. Doing it properly means proving each runtime context from the C++ side, which is a per-string investigation; the one string that was provable (`Field Mapping`) is fixed. |
| **#2083** `selection_context` `QT_TR_NOOP` context mismatch | SUPERSEDED | Fixed in #2132. |
| **#2084** chart_registry style/binding validation | SUPERSEDED | Fixed in #2132. |
| **#2116** verify budget fail-open | OUT_OF_SCOPE | Archive/reference; production verify is T04's. |
| **#2092/#2093/#2096/#2097/#2101/#2105** | OUT_OF_SCOPE | Science/processing/rs numerics — T01. |
| **#2103** | OUT_OF_SCOPE | rs NoData/type semantics — T01/T04. |
| **#1953 / #1943** teaching/lab swarm aggregates | OUT_OF_SCOPE (split) | Backend sub-items T04; UI sub-items are WP18, not reached. |

## Static candidates with no issue (from the brief, verified by reading)

| WP | Finding | Class |
|---|---|---|
| WP02 | `~AssetPreviewService()` empty and the `destroyed()` handler only removed the map entry → a dead receiver's queued worker still read the whole raster. | **REPRODUCED** (new case: `rendersStarted()` stays 0 only after the fix) |
| WP03 | `pendingRequests()` could not distinguish "registered" from "reading"; 500 superseded requests queued 500 runnables with no count of real work. | **REPRODUCED** (storm case: exactly 1 render) |
| WP08 | `adoptSpec()` writes `m_composedLayoutName` before `compiled` is read. | **CONFIRMED_STATIC** — inherited from R33 H-03, not fixed here |
| WP13 | `export.cpp` removes only this run's `delivered` pages; collateral removal of a previous generation unproven. | **NEEDS_REPRO** — R33 H-04 |
| WP17 | `curves.first()` only; `toDouble()` on a missing key yields 0. | **CONFIRMED_STATIC** — R33 H-06 |

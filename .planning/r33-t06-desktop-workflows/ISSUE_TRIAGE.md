# R33-T06 — ISSUE TRIAGE

Triage vocabulary: `NEEDS_REPRO / CONFIRMED_STATIC / REPRODUCED /
ACTIVE_PR_COVERED / FIXED_OPEN / PARTIAL / SUPERSEDED / OUT_OF_SCOPE`.

Open issues at start: **106** (labels and aggregate counts are not dynamic
evidence; each row below is what was actually read).

## Directly relevant to this track

| Issue | Title (short) | Class | Evidence / owner |
|---|---|---|---|
| **#2108** | i18n: `test_i18n_string_audit` gate fails — `nonui_zh=1088 > 1059`, 3 per-file caps | **PARTIAL → PARTIAL** | Reproduced at this HEAD (`SELFTEST PASS`, `nonui_zh=1095`). The +7 over #2108's 1088 is exactly the seven bilingual pagination literals #2132 added. **`hard_en=1` is now 0** (this track). The 3 cap overruns are `src/science_context/**` + `src/teaching/**` → **T04** (HANDOFF H-01). |
| **#2083** | `selection_context.cpp` `QT_TR_NOOP` extracted under `sicnu::app::ContextRules`, looked up via `QObject::tr` | SUPERSEDED | Fixed by #2132 (ContextRules translation). Not re-implemented. |
| **#2082** | 5 zh_CN contexts keyed by short class name vs moc qualified name | SUPERSEDED | Same as above; #2132 landed the effective-language + context work. |
| **#2084** | `chart_registry` does not validate style/binding as objects | SUPERSEDED | #2132 landed the non-object style / nested binding refusal. |
| **#2081** | i18n freshness gate fails on master (5 FAIL); `test_bilingual_delivery_ui` reads English | **PARTIAL / blocked** | `resources/translations/sicnu_zh_CN.ts` is **#2134-exclusive**; no lupdate, no TS edit, no baseline move from this track (D-09, HANDOFF H-02). |
| **#2116** | verify: Node-budget overrun renders as pass | OUT_OF_SCOPE | Archive/reference only; production verify authority is T04's. |
| **#1695** | [R17-ui][P3] UI lane cleanup list | **PARTIAL** | Read as a backlog index, not a defect set. Two of its shapes were confirmed independently here (missing-value `toDouble`, curve truncation) and are handed to the next session (H-06). |
| **#1953 / #1943** | teaching / lab swarm aggregates | OUT_OF_SCOPE (split) | Backend sub-items belong to T04; the UI sub-items are WP18 and were not reached. |

## Not issue-driven (static candidates from the brief)

WP02–WP05, WP08, WP13, WP16, WP17 have **no open issue**. They were verified as
real by reading the code at this HEAD:

| WP | Finding | Class |
|---|---|---|
| WP02 | `onComputed()` re-computed `cacheKey()` at completion, so a file replaced during the render had generation A's pixels stored under generation B's key. | **CONFIRMED_STATIC** (base source) → pinned by the A→B barrier test at HEAD |
| WP03 | `computeRange()` admitted ±Inf, `scaleToByte()` then formed `NaN` and cast it to `quint8` (UB) — every finite pixel collapsed to 0. | **REPRODUCED** (Float32/Float64 fixtures) |
| WP04 | Superseded/canceled requests still executed the full raster read; `m_inFlight` grew with click rate. | **CONFIRMED_STATIC → REPRODUCED** (1000-request burst) |
| WP08 | `adoptSpec()` sets `m_composedLayoutName` even when compose reported `compiled=false`, which enables Export/Designer on an uncompiled layout. | **CONFIRMED_STATIC** (cartography_dock.cpp:537-545, 566-569) — not fixed here, H-03 |
| WP13 | Per-page failure removes `delivered` pages (`export.cpp:286,335,395`); whether that can reach a previous successful generation is unproven without the A→B fixture. | **NEEDS_REPRO** — H-04 |
| WP17 | `curves.first()` only; `.toDouble()` on missing key → 0.0. | **CONFIRMED_STATIC** (`experiment_studio_dock.cpp:2113,2121`) — H-06 |
| WP16 | `m_droppedCount +=` is cumulative by contract; the *label* may read as current. | **NEEDS_REPRO (UI label)** — H-05 |

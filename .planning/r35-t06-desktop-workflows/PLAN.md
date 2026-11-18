# R35-T06 — PLAN / WORK-PACKAGE STATUS

`DONE` / `ALREADY_DONE_WITH_EVIDENCE` / `PARTIAL` / `BLOCKED` / `NOT_EXECUTED`.
Only the first two satisfy an oracle.

| WP | Scope | Status | Note |
|---|---|---|---|
| WP01 | Fresh residual clearing | **PARTIAL** | Baseline, PR/branch/issue state re-read (BASELINE, PR_BRANCH_MATRIX, ISSUE_TRIAGE). The R33 ledger's NOT_EXECUTED list is inherited as the work queue instead of being re-derived. |
| WP02 | Receiver/service destruction stops queued reads | **DONE** | `destroyed()` handler aborts the token; dtor flags every in-flight request; `rendersStarted()` proves the read never happened. 4 new cases. |
| WP03 | Real pending/running accounting | **DONE (preview scope)** | `pendingRequests()` (registry) vs `rendersStarted()` (real work) are now separate and both asserted; a 500-request supersede storm renders exactly once. Pool fairness / RsScanPool starvation was **not** exercised — no caller needs it yet (DECISIONS D-06). |
| WP04 | Continuous file change / selection generation | **NOT_EXECUTED** | H-08 in the R33 ledger still holds: needs `test_data_manager_panel` (app-level target). |
| WP05 | Vector preview bounds / real cancel boundary | **NOT_EXECUTED** | — |
| WP06 | Data Manager paging under pressure | **NOT_EXECUTED** | — |
| WP07 | Import batch recovery entry | **NOT_EXECUTED** | — |
| WP08 | Draft / compiled layout / last delivery | **NOT_EXECUTED** | Needs `test_r25_d_cartography_dock`, which links `sicnu_geo_rs_shell` — a separate multi-hour build (D-06). Evidence from R33 H-03 stands: `adoptSpec()` writes `m_composedLayoutName` before `compiled` is read. |
| WP09–WP12 | Layer instance / Designer / page budget / quality location | **NOT_EXECUTED** | — |
| WP13 | Multi-page/atlas failure keeps the previous delivery | **NOT_EXECUTED** | R33 H-04: `export.cpp` removes only this run's `delivered` pages; whether a previous generation can be collaterally removed is still unproven. |
| WP14–WP16 | Manifest truth / delivery actionability / history identity | **NOT_EXECUTED** | R33 H-05 for the history label. |
| WP17 | Studio report identity / curve truth | **NOT_EXECUTED** | R33 H-06: `curves.first()` and `toDouble()` defaulting to 0; the parser contract is T04's. |
| WP18 | Teaching/lab UI completeness | **NOT_EXECUTED** | — |
| WP19 | Bilingual / freshness / delivery | **PARTIAL → DONE for the gate** | Freshness measured at HEAD: `FAIL (5)` → `PASS`. One genuine missing translation (`Field Mapping`) added under its real runtime context; the four template-extraction aliases documented and added following the repository's existing convention. Canonical gate untouched. Visible desktop / QM-in-package / Windows legs **NOT_EXECUTED**. |
| WP20 | E2E closure + independent PR | **PARTIAL** | R1/R2 independent reviews recorded; E2E-01 is covered for the preview half (destruction + burst), E2E-02…E2E-05 are not executable in this session (D-06). |

## Why these two packages

* WP02/WP03 close the exact gap the R33 delivery left open and named: "销毁后无
  回调" was proven, "销毁后无昂贵读取" was not. It is verifiable with the cheapest
  target (no application link).
* WP19 is the only other package whose whole verification loop is cheap
  (a 7-second gate + `lrelease`), and it is a red gate on `master` today with a
  real user-visible string behind one of its five failures.

# Issue Triage (R28-T04) — verified against BASE_SHA source, not just labels

| Issue / sub-item | Original claim | Status on BASE_SHA `50cc3255c5` (source-verified) | Plan |
|---|---|---|---|
| #1805 **F7** orthorectification_dialog.cpp:31,155 GDALOpen on GUI thread | P3 | **CONFIRMED_OPEN** — `rpcOrGcp()` sync `GDALOpen`, called from `updateModelStatusLabel` (GUI slot) | Fix (WP02) |
| #1805 **F12** qgslayoutdesignerdialog.cpp:1228-1265 template load failure wipes layout, no undo | P3 | **CONFIRMED_OPEN** — `onLoadFromTemplate` clears inspector + `loadFromTemplate(clearExisting=true)`; XML restore failure leaves emptied layout; no undo macro | Fix (WP02/WP10) |
| #1805 **F14** cartography_dock.cpp:315-355 lifetime leak w/o terminal state | P3 | **CONFIRMED_OPEN** — per-job `lifetime` QObject only `deleteLater()`d when a terminal `taskUpdated` for the *current* `m_runningTaskId` arrives; lost/dropped terminal states leak the QObject + connections forever; dock teardown doesn't reap | Fix (WP02) |
| #1805 **F18** experiment_studio_dock.cpp:478-492 destructor join ≤60 s | P3 | **CONFIRMED_OPEN** — `~ExperimentStudioDock` joins `m_liveRunThread` + all retired threads synchronously | Fix (WP02) |
| #1561 spectral profile sync network read | P2 | **FIXED_OPEN** (issue open; fix in master per #1811 @ `923595d0e2` — `isNetworkSource`+`startRemoteSampling`) | Spot-verify with existing test; close with evidence in PR body (Refs) |
| Atlas deliveries red (named in #1812 final status) | — | **TO_REPRODUCE** — `test_cartography_production_11` `[cp11][atlas]` cases + `test_swarm_fix_agent_cartography` AG-M atlas cases run by default (skip only under `SICNU_CP11_RENDER=0`) | Reproduce on local build (WP10) |
| #1745 #1746 #1747 (P1), #1788–#1795 (P2) | fixed by #1811 | issues still open on GitHub; #1811 PR table says all fixed with tests | Not re-fixing; regression suites exist (`test_r27_t04_*`) — include in final regression run; do not claim their issues |
| #1748 #1735 #1734 (BCT P1/P2) | CI/Windows | out of scope (T05 build) | — |
| #1790–#1794 siblings | fixed by #1811 | verify via existing suites at final regression | — |

Out-of-scope guardrails: `src/app/agent_ops/**` (T03), `src/teaching_admin/**` + workflow backend (T05), `src/geospatial/**` scan pool internals (T02), algorithm math (T01).

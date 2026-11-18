# R35-T06 — PLAN / WORK-PACKAGE STATUS

`DONE` / `ALREADY_DONE_WITH_EVIDENCE` / `PARTIAL` / `BLOCKED` / `NOT_EXECUTED`.
Only the first two satisfy an oracle.

| WP | Scope | Status | Note |
|---|---|---|---|
| WP01 | Fresh residual clearing | **PARTIAL** | Baseline, PR/branch/issue state re-read (BASELINE, PR_BRANCH_MATRIX, ISSUE_TRIAGE). The R33 ledger's NOT_EXECUTED list is inherited as the work queue. |
| WP02 | Receiver/service destruction stops queued reads | **BLOCKED** | Code complete and syntax-verified; **could not be built, linked or tested** — configure never completed on this host (ACCEPTANCE "Environment", H-09). Preserved on branch `zcode/r35-t06-preview-lifecycle-wip` @ `7b926fa6c8`, deliberately kept out of the PR because an untested C++ change plus an unrun test is not a deliverable. |
| WP03 | Real pending/running accounting | **BLOCKED** | Same branch, same reason. `rendersStarted()` + the 500-request storm case exist only as source. |
| WP04–WP18 | see below | **NOT_EXECUTED** | Unchanged from the R33 ledger's handover. |
| WP19 | Bilingual / freshness / delivery | **DONE (freshness half)** | `FAIL (5)` → `PASS` on this HEAD, with the one genuine gap fixed at its real runtime context and the four extraction aliases documented. Baseline/ceilings/scan list untouched. Visible desktop, QM-in-package and Windows legs **NOT_EXECUTED**. |
| WP20 | E2E closure + independent PR | **PARTIAL** | R1 independent review done (verdict: real fix); E2E-01's preview half is blocked with WP02; E2E-02…05 not executable here. |

Details for the not-executed group, with the evidence each still needs:

| WP | Blocking input |
|---|---|
| WP04 (file watching) | `test_data_manager_panel` — application target |
| WP05 (vector preview bounds) | needs a slow/large vector fixture; no build |
| WP06 (paging pressure) | 1k/10k catalog fixture + `test_data_manager_panel` |
| WP07 (import recovery) | `test_r28_t04_import_batch` |
| WP08 (draft vs compiled) | `test_r25_d_cartography_dock` links `sicnu_geo_rs_shell` — whole application |
| WP09–WP12 | same shell target |
| WP13 (multi-page keeps old delivery) | cartography export target + real two-page fixture |
| WP14–WP16 | export/manifest + history targets |
| WP17 (Studio curve truth) | `test_experiment_studio_dock`; parser contract is T04's |
| WP18 (teaching/lab UI) | teaching UI target |

## What this session actually produced

* A red gate on `master` turned green with a **verified** root cause per string
  (not a threshold move), and one genuinely untranslated user-visible label
  fixed.
* A complete, reviewed design + implementation for the preview lifecycle gap,
  parked on a WIP branch with an explicit "do not merge before running these two
  commands" note, because the host could not produce a build.

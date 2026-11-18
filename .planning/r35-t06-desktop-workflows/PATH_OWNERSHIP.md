# R35-T06 — PATH OWNERSHIP

Precedence: active PR exclusive paths (**none at start — 0 open PRs**) > this
batch's whole-file exceptions > this track's write set > directory summaries.
The six-track whole-file owners still apply even with zero open PRs.

## Written in this session

| Path | Change | Owner |
|---|---|---|
| `src/app/preview/asset_preview_service.{h,cpp}` | WP02 destruction abort + WP03 render counter | T06 (declared) |
| `tests/test_r35_t06_preview_lifecycle.cpp` | new, 4 cases | T06 |
| `tests/CMakeLists.txt` | one appended target block at EOF | shared registration (append-only) |
| `resources/translations/sicnu_zh_CN.ts` | WP19: 1 real translation + 2 documented alias contexts | T06 (global TS owner) |
| `.gitignore` | appended r35 planning whitelist | append-only |
| `.planning/r35-t06-desktop-workflows/**` | this ledger | T06 |

## Explicitly NOT written

* `src/agent/cartography/cartography_operators.cpp`,
  `tests/test_cartography_production_11.cpp`,
  `tests/test_cartography_operators_10.cpp` — **T04**, even though the PRs that
  used to hold them are merged.
* `src/study/study_export.cpp`, `sensitivity_projection.cpp`,
  `src/experiment/evaluation.*` — **T04/T01**. The Studio curve work (WP17) is
  blocked on their validity contract, not on this track's UI.
* `src/data/**`, `src/geospatial/{fabric,identity,io}/**`, `portable.h`,
  `durable_sidecar.*`, `atomic_fs.*` — **T03**.
* `src/agent/**` runtime, AgentOps/Mission UI, managed Python — **T05**.
* `src/app/main_window_workbench.cpp` and the other `main_window_*` — T06 owns
  them, but nothing here needed them; T05's wiring requests would go through
  HANDOFF first.
* `tests/fixtures/i18n_string_baseline.json` and the audited file list — **not
  touched** (DECISIONS D-05).

## Reservation check before delivery

`gh api pulls?state=open` → 0 at start; the delivery step re-runs it, and any
new PR's file list is compared against the table above.

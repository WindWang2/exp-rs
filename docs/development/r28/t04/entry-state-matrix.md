# R28-T04 — 入口状态矩阵 (WP01)

Scope: the flows this track touched (import, ortho dialog, layout template, cartography dock, experiment studio, workflow canvas/panel, map tools, layer removal, project open). Verified against source paths + the offscreen test harnesses (real app run under a display: NOT_EXECUTED this round; noted per row).

| Entry | Available when | Acts on | Feedback | Undo/recover |
|---|---|---|---|---|
| Add Raster/Vector (single file) | always (dialog) | DataManager + display | status bar "Loaded N" / "N succeeded, M failed" | remove layer; catalog dedup |
| Add Raster/Vector (multi file) — NEW review dialog | ≥2 files picked + project catalog | ImportSession (check→proceed→journal) | one row per source: verdict, first finding + remediation, dedup note; refused/needs-choice never registered; journal failure surfaced | journal = resume entry (pending paths merge into the next batch; catalog idempotent) |
| Ortho dialog model probe (F7) | valid raster layer | RsScanPool probe | "Checking…" → RPC / GCP / neither (+refusal preview) | n/a (read-only probe; generation-guarded) |
| Layout "Load from Template" (F12) | layout open + readable .qpt | clone probe → user layout | typed error + "working layout is unchanged" on failure; statusBar on success | failure path is non-destructive by construction (probe) |
| Cartography dock compose/preflight/repair/export/produce | templates loaded + ≥1 map layer (+ composed layout for export); busy gate F14 | TaskCenter operators | progress status; typed errors; structured-error report | Stop cancels; watchdog recovers lost terminal events; staged-atomic export keeps old files |
| Experiment studio Run Live (F18) | store open + valid study spec + not busy | worker thread (cancel-observing between points) | log/status; typed failures | cancel flag; destructor detaches supervisor (≤60 s GUI freeze gone) |
| Workflow canvas node click / double-click (WP05) | canvas has nodes (session bootstraps on first click) | session + TaskPanelHost form | form shows the step's params; "Node selected: X" | params editable; staleness invalidation on edit |
| Canvas Run Full / Run up to node | canvas non-empty; one run in flight (guard FIRST) | definition + session params overlay | per-node idle/running/success/failure; typed status | Stop; late results commit to the captured session |
| Panel Run (single operator) | session+step + gates pass; not in flight | TaskCenter job | running state, result summary, typed failure | Stop; result invalidates on param edit |
| Map tools pan/identify/measure×2 (WP07 A1) | identify: ≥1 layer; others always | canvas map tool | checked button follows the active tool | switching tools unchecks (exclusive group) |
| Feature Select (WP07 A2) | any vector layer (no edit session needed) | vector layer selection | enabled/disabled + reason | n/a |
| Remove layer (WP07 A3) | layer selected | display/project | confirm; unsaved-edits warning + rollback | edits are rolled back (explicit user consent) |
| Open project (WP07 A4) | readable .qgs/.qgz + preflight pass | project + session | tool reset to pan; typed refusal leaves file untouched (#1747) | preflight-refused save keeps bytes |

NOT_EXECUTED (no display this round): real X11 app screenshots for WP12 — host HAS :0 + ImageMagick; scheduled for the GREEN phase.

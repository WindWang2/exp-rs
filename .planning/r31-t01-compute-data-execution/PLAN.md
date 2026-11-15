# R31-T01 Plan

Base: `85ce917bea0ece4df6548a677b97a72ecde8659b`. Work proceeds in small vertical slices: one public-seam behavioral test → minimal implementation → affected regression → ledger update. No WP is marked DONE from a static reading alone.

## Phase 1 — facts and output truth (WP01–WP06, WP19)

1. Finish current-master triage and fresh configure/test discovery. Preserve #2054–#2067 behavior and #2061 active-exclude.
2. WP02: distinguish declared file artifacts from value reports in existing schema/descriptor/TaskCenter. Preserve unknown legacy single-output behavior; keep multiple named outputs distinct instead of selecting the first string.
3. WP03: run real `io:inspect` / `io:doctor` / `io:subdatasets` / `io:verify_dataset` through registry→JobEngine→TaskCenter→dispatcher. Assert zero publication/catalog/input effects and truthful `verified=false`; run `io:translate` as the real-producer control.
4. WP04: reproduce reconcilePublished returning an existing path's ID while its asset is Error/missing provenance or bytes/structure changed. Reconcile content and catalog facts without revising true unchanged hits.
5. WP05: pin a resumed relative checkpoint's identity before execution; controlled CWD change must not redirect persistence.
6. WP06/WP19: rerun host-lane/wait/cancel and verifier regressions after the shared output changes; do not alter host-thread routing unless a new controlled interleaving proves a residual.

## Phase 2 — data/VRT and science (WP07–WP14)

- WP07/WP08: independent GDAL oracles for source NoData vs output fill and complete staged-warp reuse (required bands, dtype, NoData/mask, all six transform terms). WP09/WP10 use old-reader and multi-level drift scenarios; only real reproduction drives generation changes.
- WP11/WP12: move candidate and working-set admission before the containers they bound while preserving exact selected X/y/groupIds/order/seed. WP13 first adds typed bounded failure/cancel, then considers equivalent chunking only with full component-order oracles. WP14 closes the numerical/provenance chain, marking existing coverage ALREADY_DONE where appropriate.

## Phase 3 — model runtime (WP15–WP18)

- WP15: full non-secret load-contract identity and directed release over framework/device/artifact/load-contract, without guessing from the last `|` segment.
- WP16: controlled hash/sample/re-stat/concurrent-entry interleavings; one digest must stay bound to one content snapshot. Preserve unchanged fast hits and document the >192 KiB sampled residual honestly.
- WP17: account output planes/uncertainty/blend/mask/resize and multi-feed geometry in existing admission. No silent resolution reduction or second scheduler.
- WP18: real available CPU/HTTP cancellation isolation and recovery; optional ORT/GPU is ENV_LIMITED/NOT_EXECUTED when absent, never simulated as real.

## Phase 4 — acceptance and delivery (WP20)

Run the five production chains on this HEAD, plus affected legacy suites and negative restart/reopen cases. Refresh upstream and active file ownership, rebase if needed, run independent R1/R2 on the complete diff, fix blocking findings, then push only this branch and create a truthful PR. Do not merge or wait for CI.

## Current slice

WP02 mixed value report (`driver/kind/path/digest/output`) through the real registry/JobEngine/TaskCenter/dispatcher seam. Expected: value fields survive unchanged, but no outputLayerPath, commit, execution-cache store or autoLoad results. The test was added before production changes; RED execution is pending fresh configure.

# Hardening Track: temporal-change-phenology (19/20)

## Goal

Only optimize/fix/complete the existing temporal fit, irregular series,
change/breakpoint, phenology, gap-fill, and temporal prediction/composite
modules. No new product directions. Campaign 19/20, track slug
`temporal-change-phenology`.

## Non-goals

- No new time-series research directions.
- No changes to `src/core/qgs*temporal*` (QGIS-vendored code).
- MAD / MOSUM / SOS-EOS interpolated crossings / leap unwrap / 365.25 doyOf
  were fixed by #1200 and #1229 — regression only, never re-implemented.
- `src/teaching/**`, `src/app/teaching/**` and teaching shell wiring are owned
  by open PRs #1237/#1238/#1239 — untouched.

## Slices

1. Slice A (correctness): source-level deep review of
   `src/processing/algorithms/temporal/*`, `breakpoint_detection.*`,
   `phenology_metrics.*` against the track's five deep-dive goals; fix
   empirically-confirmed defects with regression oracles.
2. Slice B (test potency): regression oracles for the already-fixed
   MAD/MOSUM/SOS-EOS/leap behavior where missing, plus oracles for every new
   fix (old implementation must fail RED).
3. Slice C (independent adversarial review + closure).

## Exit conditions

- All confirmed in-scope P0/P1/P2 closed with regression oracles.
- At least one kill-proof (old implementation RED) for a behavior fix.
- Targeted oracles pass twice consecutively on the final commit.
- Independent reviewer verdict: READY (or remaining blockers listed and
  resolved).
- PR created; not merged; online CI not awaited.

# 04 — R2 test ledger (post-#1258 continuation, master e4904cd3c5)

R2 re-opened the track after R1 (#1253) landed via the #1258 semantic union.
Recon delta: no open PR touches this slice's files (verified via
`gh pr view <n> --json files` for #1277/#1278/#1280–#1284); open PR #1279
owns `src/preflight` slice B (engine/rules/provider/render/adapter) — all
preflight engine work is yielded to it. Master-side drift since the R1 base
is #1271 (`exportToWkt(char**)`, reviewed: consistent with the other call
sites, `CPLFree` on the success path) and #1248/#1266 harness evolution —
neither interferes with the passport chain. `tests/CMakeLists.txt` is
untouched (oracles extend existing test files only).

## Defects found on current master (RED evidence: source trace + failing Catch2 run on unmodified sources)

| # | Defect (master evidence) | Fix | Oracle (RED because…) | Test |
|---|---|---|---|---|
| R2-1 | `resolveAcquisition` merged observations with `NormalizeAsIs`: one instant declared `"2026-09-23 00:00:00"` (file) and `"2026-09-23T00:00:00Z"` (catalog) resolved to a **Conflicted** claim; `"September 2026"` resolved to a **Known** `acquisition.time` that no chain consumer can parse (workflow_facts `parseInstant`, suitability `QDateTime`) | `NormalizeTimestamp`: closed ISO-8601 subset (date-only; `T`\|space; optional seconds/fraction; `Z`\|`±HH:MM`\|`±HHMM`), canonical UTC spelling (midnight-UTC collapses to the date-only form — coarsest faithful representation, never a fabricated time-of-day; naive = UTC per the harness convention); unparsable → typed note `acquisition.time_unparseable` + unknown | "same instant resolves once" and "unparsable is typed unknown" fail on master (conflict raised / Known garbage); canonical round-trip + genuine-conflict guards pass both before and after | `test_scientific_state_geo` `[r2][acquisition_time]` (10 cases) |
| R2-2 | `resolveBands` paired file band *i* with the catalog structure mirror's band *i* with **no structure check**; a stale mirror (asset re-registered after the file changed) fed its roles/NoData into the file's bands as Known `catalog:structure` facts about bands that never declared them | Pairing requires `catalog->bands.size() == datasetBands.size()` when a dataset is present; on mismatch the mirror contributes nothing and a state-level note `bands.catalog_structure_mismatch` records the gap (catalog-only passports unchanged) | "stale mirror cannot feed roles" fails on master (band 1 labelled "red" by a 3-band mirror over a 2-band file); equal-count positive control and catalog-only control pass both before and after | `test_scientific_state_review` `[r2][catalog_pairing]` (3 cases) |

## Disposition of the recorded P3s (unchanged by R2)

- `sarFacts` `"dn"` substring — unreachable with today's vocabulary; left
  alone to avoid loosening `sigma0*` matching (R1 decision).
- `.front()` single-source reads (`SICNU_SAR_DOMAIN`, `SICNU_NUMERIC_SCALE`,
  band `WAVELENGTH`/units, `CLOUDCOVER`, `SICNU_QA_VOCABULARY`) — the only
  `DatasetFacts` producer is the GDAL collector, whose per-domain metadata
  is unique-keyed (GDAL `SetMetadataItem` replaces), and the one synthetic
  item (`NO_DATA_VALUE`) is `contains()`-guarded: duplicates are not
  reachable today. The multimap-shaped contract makes them a latent trap,
  but per the track rules a non-reproducible concern is recorded, not
  refactored.
- `supervised=false` per-entry opt-out — needs a product decision (R1).
- Preflight slice B — future direction, owned by open PR #1279.
- Collector fixture directory hygiene — follow-up, untouched here.

## Verification

(Run on the R2 branch; `-j2`, `ctest --test-dir build-dev -j2 -R` …)
- RED (unmodified master sources): `test_scientific_state_geo [r2]` →
  2/8 passed (guards only); `test_scientific_state_review [r2]` → 2/3
  passed (controls only).
- GREEN: full `scientific|suitability|platform5` face — see PR body for the
  final counts and the two consecutive passes.

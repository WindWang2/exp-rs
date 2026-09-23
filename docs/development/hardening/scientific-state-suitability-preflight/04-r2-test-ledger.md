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

(Run on the R2 branch; `-j2`, sdk-lane binaries under `build-dev`)
- RED (unmodified master sources): `test_scientific_state_geo [r2]` →
  2/8 passed (guards only); `test_scientific_state_review [r2]` → 2/3
  passed (controls only).
- GREEN, two consecutive passes after review round 1 (below): 9/9 suites —
  core 76/12, resolver 78/18, geo 171/37, provenance 71/15, diff 41/12,
  teaching 31/8, fixtures 102/9, review 39/10, gdal 64/4 (assertions/cases).
- Face-selection notes: the `suitability` family was excluded by type-level
  argument — `sicnu::suitability::DatasetFacts` is an independent type that
  never enters the passport resolver. The catalog adapter lane
  (`test_scientific_state_catalog`, Qt/DataManager) was excluded from local
  execution (its fixture `acquisitionTimeIso` inputs are already-canonical
  strings, an identity under the normalizer) — the compiler ICEs seen on
  Qt-heavy TUs with the rolling GCC 16 snapshot are master-preexisting and
  out of this delta's blast radius. `test_platform5` lives in the
  `test_mapspec` Qt mega-target and does not link `Sicnu::ScientificState`;
  no harness TU changed in R2.

## Review round 1 (independent adversarial reviewer) — all fixed with oracles

| Sev | Finding (verified counterexample) | Fix | Oracle |
|---|---|---|---|
| P1 | Offset range check used truncating division on the composed value: `"…T00:00:00-99:00"` accepted (4-day silent shift), `"…+00:99"` accepted | Per-digit-field bounds (hours ≤ 23, minutes ≤ 59) before composing | geo `[r2]` "zone offsets beyond ±23:59…" (4 spellings → unparseable) |
| P2 | `midnightUtc` ignored a nonzero fraction: `00:00:00.5Z` collapsed to `2026-09-23`, merging distinct instants | midnight collapse requires `fraction.empty()` | geo `[r2]` "a nonzero fraction at midnight…" (stays a distinct instant → real conflict) |
| P2 | `formatDate` buffer[11] truncated the year-10000 rollover to `"10000-01-0…"` (self-unreadable passport fact) | buffer[16] | geo `[r2]` "…survives the year-10000 zone rollover" (`9999-12-31T23:30:00-01:00` → `10000-01-01T00:30:00Z`) |
| P2 | Equal counts without index alignment still paired a `{2,3}` file axis against a `{1,2}` mirror | Pairing additionally requires aligned 1-based indices; note detail updated | review `[r2]` "equal band counts with misaligned indices…" |
| P3 | Decimal minutes `05:06.5` were re-weighted as a fraction of seconds | `.` after minutes (no SS) refused — outside the closed subset | geo `[r2]` "decimal minutes are outside the closed subset…" |

## Review round 2 (same reviewer, fresh-eyes on the fixed tree) — READY

All five counterexamples re-probed fixed; pointer bounds, offset magnitude
(cap 86340 s) and the `sawSeconds` placement re-checked; both oracle files
green (geo 171/37, review 39/10). Residual P3, recorded not fixed:
`"0000-01-01T00:30:00+01:00"` crosses into the proleptic year −1 and
canonicalizes to a `"-001-…"`-form no consumer re-parses — year-0000
acquisition metadata is not a real input; if it ever becomes one, the
closed subset should refuse years < 1 explicitly.

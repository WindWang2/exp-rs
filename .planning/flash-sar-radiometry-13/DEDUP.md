# DEDUP — flash-sar-radiometry-13 (Track E)

Live dedup against origin/master `79adfe78` (2026-09-21), 3 open PRs, 0 open
issues, 33 remote branches (all merged-history residue except the three open-PR
heads). Method: PR body/diff read for #1131 (merged predecessor) and the three
open PRs; file-level grep census over `src/processing/algorithms/sar/`,
`src/operators/rs/rs_sar_*`, `src/geospatial/products/`.

## Already implemented (do NOT redo)

1. Declared-calibration fail-closed guards in `rs:sar_calibrate` and
   `rs:sar_backscatter` (SAR 12.0, #1131): re-calibration refusal, unrecognized
   token refusal, fromCalibration cross-check. Covered by existing tests.
2. `rs:sar_speckle` state propagation incl. multitemporal reference-scene
   semantics (#1131).
3. Declared `SICNU_SAR_DOMAIN=db` refusals in the filter family (older).
4. GF-3 SAR sidecar parsing at declared-metadata level (ADR 0159) — deliberately
   does NOT invent sigma0 calibration constants (confirmed in
   `cn_product_metadata.cpp` comment). No LUT vectors exist anywhere in
   `src/geospatial/products/` (grep: no LUT structures).

## Real gaps (this track's scope, all with code evidence)

1. **terrain_flatten / terrain_correction**: only refuse declared `db` domain;
   a declared `gamma0`/`beta0`/`dn`/derived/unknown input is silently flattened
   as if sigma0 and relabeled gamma0 (input-state verification absent; output
   state hardcoded). Evidence: `rs_sar_terrain_flatten_operator.cpp:188-192`
   (domain-only check) and kernel `sar_terrain.cpp:266` (unconditional
   `writeSarOutputMetadata(dst, "gamma0", ...)`).
2. **geocode**: copies the raw `SICNU_SAR_CALIBRATION` token but never writes
   `SICNU_RADIOMETRIC_STATE`; the gamma0 product band applies
   `sin(θL)/sin(θ0)` which is only lawful for sigma0 input; no input-state
   verification. Evidence: `rs_sar_geocode_operator.cpp:565-577`.
3. **ratio / texture**: no state written at all — outputs would re-ingest as
   undeclared DN and could be re-calibrated as if they were backscatter.
   Evidence: `rs_sar_ratio_operator.cpp` / `rs_sar_texture_operator.cpp`
   (no setMetadataItem of state keys).
4. **DN LUT calibration**: `rs:sar_calibrate` only supports the constant
   `calibrationA`; no LUT parsing, no declared-LUT metadata key, no refusal for
   missing LUTs. Evidence: `rs_sar_calibrate_operator.cpp` + `sar_calibration.h`.
5. **No machine-readable census**: no test enumerates the 23 `rs:sar_*`
   operators against declared state rules; no E2E chain provenance test.

## Explicitly out of scope (overlap avoidance)

- Temporal SAR fusion (`rs:temporal_sar_fusion`, PR #1135 open) — forbidden by
  track ownership. `rs:sar_temporal_stats` / `rs:sar_temporal_events` are
  different operators in different files; they receive census entries
  (exemption class) but no behavioral changes.
- `data/labs/lab12_sar_processing.lab.json` (PR #1136) — untouched.
- InSAR algorithm expansion — no new algorithms; state-metadata defects only.
- Capability sidecars / algorithm_meta / help data — untouched.

## Pivot log

None. No live PR implements any gap above.

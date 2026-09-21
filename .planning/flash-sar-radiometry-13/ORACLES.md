# ORACLES — flash-sar-radiometry-13 (Track E)

All gates run with `QT_QPA_PLATFORM=offscreen`, `CTEST_PARALLEL_LEVEL=1`,
`-j1` builds. Two consecutive passes required before PR.

## Build gate

- O-BUILD: `cmake --build build-dev -j1 --target test_sar_operators
  test_sar_kernels test_speckle_filter test_sar_polsar test_sar_insar
  test_sar_complex test_sar_geocoding test_sar_radiometric_state` exits 0.

## Track-specific oracles

- O1 **Census completeness**: every operator id registered with the `rs:sar_`
  prefix in `RSOperatorRegistry` has an entry in the machine-readable census
  table (`tests/test_sar_radiometric_state.cpp`), and the table contains no
  entry without a registered operator (no stale/ghost rows).
- O2 **Census behavioral gate**: for each first-class radiometric operator
  (calibrate, speckle, terrain_flatten, terrain_correction, geocode, ratio,
  texture) the census runs the operator on a synthetic fixture and asserts the
  output's declared state equals the table's expectation. Deleting any state
  write fails this gate (mutation-proven).
- O3 **Terrain state rules**: terrain_flatten/terrain_correction accept
  declared `sigma0` (and legacy undeclared with a logged warning) and produce
  gamma0; declared `gamma0`/`beta0`/`dn`/derived/unknown input is a typed
  refusal with no partial output.
- O4 **Geocode state**: declared `sigma0` (or legacy undeclared + warning)
  geocodes; other declared states are typed refusals; output writes
  `SICNU_RADIOMETRIC_STATE`, `SICNU_SAR_CALIBRATION`, and the per-band states
  key; reopen/readback preserves them.
- O5 **Derived semantics**: ratio output declares `sar_pair_metric`, texture
  output declares `sar_texture`; neither claims sigma0/gamma0/beta0; a
  subsequent `rs:sar_calibrate` on either is a typed refusal (no re-calibration
  of derived products).
- O6 **LUT calibration**: synthetic LUT sidecar + hand-computed per-row pixel
  oracle passes; missing/malformed/mismatched-count LUT is a typed refusal
  with no partial output; no constant-A fallback on the LUT path.
- O7 **E2E chain**: `import(DN+LUT) → calibrate → speckle → terrain_flatten →
  geocode → ratio` — every step's declared state matches the expected
  transition table; double calibrate / wrong fromCalibration / missing LUT /
  conflicting metadata all fail closed with no partial output; full-chain
  reopen/readback keeps metadata + provenance.
- O8 **Regression**: existing suites stay green — test_sar_operators,
  test_sar_kernels, test_speckle_filter, test_sar_polsar, test_sar_insar,
  test_sar_complex, test_sar_geocoding (two consecutive passes each).
- O9 **Mutation potency**: deleting one state write (terrain flatten's
  radiometric-state write) makes O2/O4 fail; reverting the ratio derived-state
  write makes O5 fail.
- O10 `git diff --check` clean.

## Global oracles

Worktree from fresh origin/master; dedup done; ownership respected; two
consecutive green passes; independent review P0=P1=0; PR created, not merged,
no CI wait.

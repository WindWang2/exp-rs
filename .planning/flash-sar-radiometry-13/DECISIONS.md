# DECISIONS — flash-sar-radiometry-13 (Track E)

Autonomous decisions (autonomy=full); each recorded with candidates, rationale
and the taken default.

## D1 — Derived-state vocabulary lives in BOTH state keys

Candidates: (a) derived token only in `SICNU_RADIOMETRIC_STATE`; (b) derived
token in both `SICNU_SAR_CALIBRATION` and `SICNU_RADIOMETRIC_STATE`.
Taken: **(b)**. Every radiometric SAR operator already writes both keys with
the same token (documented convention, sar-domain.md §15.1). Writing both keeps
one convention, makes every existing fail-closed guard (which reads
`SICNU_SAR_CALIBRATION` via `declaredCalibrationToken`) reject derived products
automatically, and needs only a message-quality patch plus speckle propagation
extension. Cost: the calibration key now also carries non-calibration derived
tokens — accepted because the guards classify, never assume.

## D2 — Undeclared input state = legacy accept with warning; declared non-matching = refusal

Candidates: (a) refuse undeclared inputs everywhere (strictest); (b) accept
undeclared with a logged warning (legacy-compatible).
Taken: **(b)** for terrain_flatten/terrain_correction/geocode. Rationale:
existing shipped tests (`test_sar_operators.cpp` terrain cases,
`test_sar_geocoding.cpp`) build fixtures without declarations; SAR 12.0 set the
same precedent for `rs:sar_calibrate` on undeclared DN. A warning is not a
silent assumption; declared states that contradict the algorithm's domain are
typed refusals.

## D3 — Geocode per-band states via an additive key

The geocode product is inherently mixed (band 1 sigma0 backscatter, band 2
gamma0, bands 3-5 geometry). A single dataset-level token cannot describe it.
Taken: dataset-level `SICNU_RADIOMETRIC_STATE`/`SICNU_SAR_CALIBRATION` =
`sigma0` (the radiometric input band's state, propagated), plus a new additive
key `SICNU_SAR_GEOCODE_BAND_STATES` = `sigma0,gamma0,incidence_deg,
incidence_deg,mask_class` and `result["bandStates"]`. Backward compatible:
existing keys unchanged; consumers that ignore the new key are unaffected.

## D4 — Ratio/texture declare derived tokens; no backscatter impersonation

Taken: ratio → `sar_pair_metric`; texture → `sar_texture`. Both tokens are
unknown to `normalizeCalibration` (so re-calibration refuses) and recognized by
the new `isSarDerivedState`. The existing `SICNU_SAR_RATIO_OUTPUT` /
`SICNU_SAR_TEXTURE_MEASURES` keys keep carrying the exact metric/measure
identity. Not taken: relabeling outputs as gamma0 (explicitly forbidden by the
track prompt) or writing nothing (the current silent drop).

## D5 — LUT format: one value per input row, exact count, no interpolation

Candidates: (a) Sentinel-1 annotation-XML LUT parsing; (b) generic resampled
LUT with interpolation; (c) per-row A-vector sidecar with exact row count.
Taken: **(c)**. The repo cannot read Sentinel-1 XML annotations today (no
parser exists; the GF-3 adapter deliberately invents no constants), so (a)
would be pretending. (b) silently resamples a calibration contract — the exact
failure class this track closes. (c) is the minimal reliable path: a plain-text
sidecar (one finite >0 float per line), count must equal the raster height,
referenced by `SICNU_SAR_CALIBRATION_LUT` metadata (resolved relative to the
raster) or the `calibrationLut` parameter. Interpolation applicability is
therefore defined as *none* — a count mismatch is a typed refusal, not a
resample. Formula per pixel/row r: `sigma0 = (DN² − noise)/A(r)²`, same
noise/NoData semantics as the constant path.

## D6 — Speckle propagates recognized derived tokens

`rs:sar_speckle`'s rule is "propagate the input's declared state". Taken: the
kernel propagates the full recognized token (canonical or derived) instead of
only `readCalibration` output, so filtering a derived product no longer drops
its state.

## D7 — Census is table + behavioral gate, not a hand-written list alone

A static table alone cannot detect a deleted state write. Taken: the census
test (a) enumerates `RSOperatorRegistry::instance().operatorNames()` for the
`rs:sar_` prefix and requires a table entry per id and vice versa
(completeness), and (b) runs each first-class radiometric operator on a
synthetic fixture and asserts the output state against the table
(behavioral). Complex/geometry/mask operators carry explicit exemption entries
with the reason recorded in the table.

## D8 — Guard message quality for derived tokens

The two existing guards (`rs:sar_calibrate`, `rs:sar_backscatter`) already
refuse unknown tokens; a derived token would produce the generic
"unrecognized" message. Taken: add a derived-aware branch with a precise
message ("declares a derived SAR product … not a backscatter raster") —
6 lines per operator, no semantic change to the refusal set.

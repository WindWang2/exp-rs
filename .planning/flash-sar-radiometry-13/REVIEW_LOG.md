# REVIEW LOG — flash-sar-radiometry-13 (SAR Radiometric State 13.0)

Independent reviewer: a separate general-purpose agent (not the implementer),
read-only, never ran builds. Base for both passes: `origin/master...HEAD`
(`79adfe78a` → final commit).

## First pass — verdict SHIP-WITH-FIXES (0 P0 / 1 P1 / 3 P2 / 9 P3)

| # | Sev | Finding | Disposition |
|---|---|---|---|
| 1 | P1 | New `calibrationLut` schema parameter broke `test_drift_projection_10`'s schema↔sidecar parameter projection (green on master). | **FIXED** (`1d4a34752`): sidecar row added to `data/processing/algorithm_meta/capability/rs-sar-calibrate.json` (hand-maintained v2 sidecar, not a `generateCatalog` product). Gate re-run: 2733/3 green. |
| 2 | P2 | `parseCalibrationLut` used unbounded `readAll()` on a data-controlled path. | **FIXED**: streamed `QTextStream` parsing, size pre-check (per-row byte budget), early exit once the row budget is exceeded. |
| 3 | P2 | Geocode stamped a `sigma0` claim on legacy undeclared inputs with only a log warning (a log line does not travel with the artifact). | **FIXED**: `SICNU_SAR_STATE_ASSUMED=sigma0_legacy_undeclared` persisted on the `OkUndeclared` path in geocode + terrain flatten/correction; asserted present-on-undeclared and absent-on-declared. |
| 4 | P2 | `gamma0` had two incompatible meanings in one vocabulary (backscatter geometric normalization vs terrain-flattened RTC). | **FIXED**: `sar-domain.md` §16 states the family semantics; geocode refusal message and E2E comment corrected. |
| 5-13 | P3 | Path traversal on the declared LUT path; param/declared resolution asymmetry undocumented; misleading refusal messages; multi-band LUT footgun; lossy per-band states key (both incidence bands `incidence_deg`); untested ratio conflict/derived branches; inert-but-validated `calibrationA`; latent cross-vocabulary collision on `SICNU_RADIOMETRIC_STATE`; missing LUT edge-case tests. | **FIXED**: directory containment (declared path), schema/doc documentation, message wording (actual key named; undeclared vs unrecognized distinguished), multi-band LUT warning, per-band key `sigma0,gamma0,incidence_deg,local_incidence_deg,mask_class`, new tests for ratio refusals + LUT precedence/containment/count-direction/0-byte, `calibrationA` validated only on the constant path, cross-vocabulary assumption documented in `sar_metadata.h`. |

Verified clean in the first pass: census table matches the registry 23/23 with no ghost
rows; no partial output on any refusal path (every guard precedes output creation);
per-row indexing correct across tile boundaries; LUT arithmetic and NoData policy match
the constant path; all existing SAR fixtures take the legacy path and stay green; no
consumer of the state keys is perturbed by the derived tokens; `git diff --check` clean.

## Second pass — verdict SHIP (P1 + both P2s verified fixed; 10 P3 follow-ups)

All P1/P2 fixes re-verified in code, including the sidecar name-set exactness (10/10
parameters, no missing/phantom), the seven LUT streaming cases, the three
`SICNU_SAR_STATE_ASSUMED` write sites (each guarded, in scope), and the gamma0
vocabulary consistency across doc, code and tests.

Remaining P3s, all fixed in `ef8685193`:

1. Traversal test was vacuous (outside LUT did not exist) → the outside LUT now exists
   with valid row-exact content, so only containment can refuse.
2. Ratio conflict test was masked by the mismatch check → both inputs now conflict.
3. Per-row byte budget raised to 256 B with an accurate message.
4. `QTextStream::status()` checked after the loop (I/O errors reported as such).
5. Doc §16 records the size-budget refusal.
6. `SICNU_SAR_STATE_ASSUMED` asserted for all three operators (geocode legacy case added).
7. LUT-without-trailing-newline case added.
8. Containment hardened: canonical-path comparison with lexical fallback + root guard.
9. Derived-product refusal message text asserted (`ContainsSubstring("derived SAR
   product")`).
10. `test_catalog_size` byte-budget concern: the envelope already exceeds its 176 KiB
    budget by ~74 KiB on pristine master (proven by stash experiment — identical failure
    with this branch's sources removed), so the branch's ~350 bytes are immaterial;
    recorded as a pre-existing red gate in the PR body.

## Verification after remediation

Two consecutive full passes (build exit 0, 0 errors):
`test_sar_radiometric_state` 373/14, `test_sar_operators` 675/26, `test_sar_kernels` 89/12,
`test_speckle_filter` 15869/26, `test_sar_polsar` 143/8, `test_sar_insar` 266/10,
`test_sar_complex` 378/7, `test_sar_geocoding` 5769/8, `test_drift_projection_10` 2733/3 —
26,999 assertions / 107 cases green on both passes.

Potency evidence: pristine-source run of the new gate fails (8/12 cases at the first
commit point; the band-states assertions fail after remediation); deleting
terrain_flatten's radiometric-state write fails 3 cases; deleting ratio's derived-state
writes fails 2 cases.

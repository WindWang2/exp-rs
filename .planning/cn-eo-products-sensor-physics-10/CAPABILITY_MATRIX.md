# CAPABILITY MATRIX — cn-eo-products-sensor-physics-10 (Phase 0 audit @ 7d78059d1a)

## A. Audit of PR #956 (existing real capability)

| Area | State | Evidence |
| --- | --- | --- |
| Identity | filename-token based, 11 supported patterns; separator-anchored `nameHasToken` prevents substring false positives | `cn_product_metadata.cpp:317-356` |
| Recognized-but-unsupported refusals | 11 reason branches (GF-1B/C/D, HJ1C, GF3, GF4, GF5, GF7, ZY1, ZY5, HJ2, HJ1-IRS, CBERS) — plain strings, no structured "what would be needed" | `cn_product_metadata.cpp:362-427` |
| Sidecar parsing | path-aware expat scanner, 2 generations implicitly merged via tag whitelist (legacy MetaInfo + current ProductMetaData) — generation NOT reported | `cn_product_metadata.cpp:481-794` |
| Unknown XML elements | silently ignored (no diagnostics, no count) — violates §D requirement | scanner has no unmatched reporting |
| Calibration | per-band `<BandCalibration>` records + legacy flat GainVal/OffsetVal; partial-declaration safe (per-element record) | `cn_product_metadata.cpp:705-780` |
| Band roles | data-driven JSON, fail-closed, role_reason contract | `cn_product_metadata.cpp:899-940` |
| Sensor-key resolution | inventory/ModeID-driven pan-vs-MS (ZY3 NAD, GF PMS) | `cn_product_metadata.cpp:1079-1095` |
| Import operators | 3 operators share header machinery; stacking via `stackToGeoTiff`; metadata stamped; declared/missing fields reported | `rs_cn_import_operator.h` |
| RPC (.rpb) | NOT parsed, NOT reported — gap vs §E | grep: no `.rpb` handling in products |
| PAN/MS relationship | implicitly handled (sensor key) but not declared in output | `cnImportResult` has no pan/ms pairing info |
| Stereo relationship | absent (GF-7 unsupported) | — |
| Capability metadata | CN import operators absent from `data/processing/algorithm_meta/capability/` (only landsat) | ls 2026-09-13 |
| Agent query | no "what is this product / what bands / is it usable" surface | grep: no products in src/agent |
| GUI | `test_product_import_dialog.cpp` covers generic product dialog; CN-specific paths untested there | file exists |

## B. Sensor-truth stores (drift found)

| Store | Content | Semantics | Linked? |
| --- | --- | --- | --- |
| `data/products/band_roles/gaofen.json` | gf1_pms…gf6_wfv bands | wavelength = documented **range midpoint** | no link |
| `data/spectral/sensors.json` `gf-pms` | 4 bands | wavelength = nominal **band center** + FWHM (Gaussian SRF) | no link |
| compiled `kSupportedPatterns` | satellite/mode/sensorKey | identity | no link |

Drift example: GF-1 PMS B1 = 470 nm (range midpoint 0.42–0.52 µm) vs spectral store
485 nm (nominal center, FWHM 70). Both are legitimate *different physical quantities*
but nothing states that; a consumer cannot tell.

## C. Gap matrix → work packages

| Gap | § | Package |
| --- | --- | --- |
| No unified sensor profile registry (platform/instrument/mode/GSD/polarization/cal rules/constituents) | C | A |
| No explicit sidecar generation detection/reporting | D | A |
| No unknown-element diagnostics | D | A |
| No forward-compat policy statement (unknown generation → typed verdict, not garbage) | D | A |
| No RPC/.rpb declared-field capture | E | A/C |
| No PAN/MS pairing, tile/scene identity declaration in results | E | C |
| No optional calibration application (DN→radiance) with provenance | E/F | C |
| HJ-2 CCD, GF-4, GF-7, ZY-1 02C/02D, CBERS-04, GF-3 unsupported (typed refusal only, static reasons) | B | B |
| No unified ImportPlan service (identify→…→provenance) shared by all surfaces | F | C |
| No capability metadata / agent query surface for CN products | §四 | D |
| Encoding/locale path tests (Chinese paths) missing | G | E |
| Large-directory enumeration bounded at 512 entries — behavior undocumented/untested | G | E |

## D. Technical debt priorities

1. `productBandRole`/`productBandWavelengthNm` in `product_adapters.cpp` carry compiled-in
   Landsat/S2 wavelength tables — third band-truth store (consolidate under registry in
   this track only for CN sensors; Landsat/S2 consolidation is out of scope, recorded).
2. `cnLocateSidecarXml`/`cnLocateImageTiff` do bounded scans (512) — fine, but
   "ambiguous multi-TIFF directory" returns "" which surfaces as generic FileNotFound
   (should be a typed ambiguous-diagnosis).
3. `detectProductKind` routes recognized-but-unsupported CN names to `Unknown` —
   auto-read refuses, but the refusal loses the identity reason (identity is re-derivable
   via cnIdentifyProduct; the ImportPlan service will surface it).

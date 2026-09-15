# CURRENT_ARCHITECTURE — radiometric domain (master @ a5b11b7f10)

## Authorities (single owners — do not duplicate)

| Authority | Owner | Consumers |
|---|---|---|
| Radiometric state vocabulary (`SICNU_RADIOMETRIC_STATE`, `SICNU_NUMERIC_SCALE`, 5-state domain) | `src/processing/algorithms/satellite_products.h:198-246` + ADR 0114 | importers, change detection preflight, spectral indices, SAR metadata (same key), canonical metadata |
| DN→radiance/TOA/BT formulas + sensor metadata parsing (MTL/MTD/GDAL) | `RadiometricCalibration` namespace (`radiometric_calibration.{h,cpp}`) | calibration operator, DOS chain, product import |
| DOS/QUAC atmospheric methods | `AtmosphericCorrection` namespace (`atmospheric_correction.{h,cpp}`) | atmospheric operator + aliases, QGIS provider algorithm |
| Terrain illumination (Horn, illumination cosine, Cosine/CC/Minnaert fits) | `TopographicCorrection` (`topographic_correction.{h,cpp}`) | `rs:topographic_correction` operator |
| QA band masking (Landsat QA_PIXEL / S2 SCL / generic bitmask) | `QaMask` (`qa_mask.{h,cpp}`) | `rs:apply_mask`, dialogs |
| Operator registry | `REGISTER_RS_OPERATOR` + factory map in `src/operators/rs/rs_operators_init.cpp` | workflow, CLI, agent capability catalog |
| Scientific contracts / domain probe | `src/processing/contracts/scientific_contracts.h`, policy `docs/processing/grid-and-radiometric-policy.md` | streaming kernels, preflight |
| Help catalog | `src/processing/algorithm_help_catalog.cpp` | dialogs, agent help |

## Seams / gaps this track fills

- **Solar-earth geometry** — absent everywhere → new `SolarGeometry` (Spencer/NOAA closed
  forms; pure function core, no I/O).
- **Transition authority** — vocabulary exists but nothing binds transitions to required
  inputs or emits provenance → new `RadiometricTransition` over the string vocabulary.
- **Atmospheric provider seam** — `Method` is a closed enum; no extension point → new
  `AtmosphericCorrectionProvider` interface + registry + DOS/QUAC adapters, typed refusal for
  unregistered providers.
- **BRDF/view-angle normalization** — absent (no view-zenith code at all) → new
  `BrdfNormalization` (Ross-Li kernels + empirical pair normalization), angle metadata keys
  `SICNU_{SUN,VIEW}_{ZENITH,AZIMUTH}`.
- **Radiometric QA flags** — masks exist; per-pixel flag vocabulary + calibration-chain
  propagation absent → new `RadiometricQa`.

## Data flow (target, after this track)

```
DN --(RadiometricCalibration + SolarGeometry)--> radiance/TOA
   --(AtmosphericCorrectionProvider [DOS/QUAC built-in | future 6S LUT])--> surface reflectance
   --(BrdfNormalization [needs SICNU_* angle keys])--> normalized surface reflectance
   --(TopographicCorrection [existing])--> terrain-illuminance-corrected
QA flags (RadiometricQa) propagate alongside every step; RadiometricTransition plans each
edge (required inputs, legality) and emits provenance records.
```

State markers after each step: `digital_number` → `radiance` → `toa_reflectance` →
`surface_reflectance` (`brightness_temperature` thermal branch). BRDF/terrain/QA steps
preserve the unit state (angle/mask corrections are multiplicative or flag-only).

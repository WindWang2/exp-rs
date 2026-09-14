# D13 · ADR 0158 — Radiometric Physics State System (FSM contract)

- **Status:** Accepted (D13)
- **Owners:** `src/core/radiometric_state.{h,cpp}`, consumers: calibration/atmospheric
  operators, agent physics tools, GUI workbench.

## Context

Radiometric products in exp-rs carry only weak string labels today. Nothing prevents an
uncalibrated DN raster from being fed to an atmospheric-correction or vegetation-index
operator, producing silently wrong science (magnitudes off by orders of π·d²/ESUN factors).

## Decision

A five-state radiometric unit system with a directed-acyclic transition FSM:

```
DigitalNumber ──▶ Radiance ──▶ ToaReflectance ──▶ BoaReflectance
       │              │  ▲             │
       │              └──┼─────────────┼──▶ BrightnessTemperature   (Radiance→BT only)
       └──▶ ToaReflectance (sensor reflectance coefficients shortcut)
```

Lawful single-step edges:

| From | To | Operator |
|------|----|----------|
| DigitalNumber | Radiance | gain/bias calibration |
| DigitalNumber | ToaReflectance | reflectance coefficient shortcut (Landsat OLI Mρ/Aρ) |
| Radiance | ToaReflectance | π·L·d²/(ESUN·sin θe) |
| Radiance | BoaReflectance | DOS on radiance |
| Radiance | BrightnessTemperature | Planck inverse T = K2/ln(K1/L+1) |
| ToaReflectance | BoaReflectance | 6S LUT closed-form inversion |
| X | X | identity no-op is always lawful |

Any edge not in the table is **unlawful** — notably DN→BOA, DN→BT, TOA→BT, and every
backwards inversion (BOA→DN etc.). Unlawful transitions throw
`RadiometricStateMismatchException` at the preflight seam; operators never proceed.

### Metadata contract

- Key: `SICNU_RADIOMETRIC_STATE` (QgsMapLayer custom property; GDAL `DEFAULT` domain when
  persisted).
- Values (case-insensitive parse): `DIGITAL_NUMBER`, `RADIANCE`, `TOA_REFLECTANCE`,
  `SURFACE_REFLECTANCE`, `BRIGHTNESS_TEMPERATURE`.
- Unrecognized strings parse to `DigitalNumber` (rawest fail-safe interpretation).
- Missing marker ⇒ `DigitalNumber`.

## Consequences

- Every radiometric operator that changes a band's unit calls
  `setLayerRadiometricState` on success.
- Every operator that requires an input unit calls `validateBandPreflight` first; tests
  assert the exception, not error strings.
- Backwards-compat: absence of the key degrades to DN, which only permits DN→{Radiance,
  TOA} — exactly the physically safe subset.

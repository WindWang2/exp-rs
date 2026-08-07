# ADR 0081: Spectral Library Domain

## Context

The hyperspectral stack can match (SAM/SID), unmix, extract endmembers, and
resample spectra — but reference spectra had to be supplied inline as JSON
arrays; there was no reusable, persistent spectral library object. The
mission's D2 asks for spectral library management with import/export.

## Decision

1. **Domain module** (`SpectralLibrary`, `src/processing/algorithms/spectral_library.{h,cpp}`):
   - `Entry` — name, optional material / source labels, band values, optional
     wavelength grid (nm).
   - `Library` — ordered entries with JSON round-trip (`toJson` / `fromJson`),
     file persistence (`save` / `load`), and helpers `bandCount()` /
     `wavelengths()` that return 0/empty when entries are inconsistent. The
     stored `spectrum` shape matches the `refs` / `endmembers` arrays of
     `rs:sam_classify` / `rs:spectral_unmixing`, so a library entry feeds
     those operators directly.

2. **Format**: `{"format": "sicnu-spectral-library", "version": 1,
   "bandCount": N, "wavelengths": [...], "entries": [...]}` — band count and
   the shared wavelength grid are stored once.

## Consequences

- Custom spectral libraries are importable/exportable and reusable across
  matching, unmixing, and (with D4 resampling) cross-sensor workflows —
  without embedding spectral knowledge in visualization widgets.
- The module is pure and headless-testable: round-trip, empty / no-wavelength
  libraries, and malformed-input rejection are pinned (33 assertions).
- A UI surface (library editor, D10 spectral workbench) can build on this
  domain without rework.

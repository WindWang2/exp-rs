# ADR 0116: Change-Detection Dialog Aligned with the Backend

## Context

The `rs:change_detection` backend grew ratio/CVA methods (slice 8), mask
threshold strategies including statistical (slice 57), morphological cleanup
and the minimum mapping unit — but the change-detection dialog still offered
only difference / normalized_difference / change_mask with a bare manual
threshold, and never set `makeMask`. Backend capability and UI capability were
out of alignment.

## Decision

- Method list gains **Ratio** and **CVA** (CVA uses all bands of both rasters;
  the band pickers stay but are harmless for it).
- New "同时输出变化掩膜" checkbox (`makeMask`) and a mask-parameter section
  shown when the mask is requested (checkbox or the legacy change_mask
  method):
  - threshold strategy combo (manual / Otsu / percentile / statistical) with
    strategy-specific spins (percentile, statistical k, both defaulted from
    the operator schema),
  - morphological cleanup combo + iterations,
  - minimum mapping unit spin (`minAreaPixels`).
  The legacy change_mask method forces manual (its old path only supports it);
  the strategy combo disables in that case.
- `buildParams()` was extracted (public, testable) and `onRun()` now uses it —
  matching the other operator dialogs; mask parameters are omitted at their
  defaults, explicit options surface in the JSON.

## Consequences

- The dialog now exposes the full backend surface (5 methods, 4 threshold
  strategies, cleanup, MMU) with the DoD "advanced-but-defaulted" pattern; the
  Processing Registry operator remains the single execution seam.
- Pinned headlessly by `test_change_detection_dialog` (8 sections): method
  list, no-mask default, mask defaults, statistical/percentile parameter
  surfacing, cleanup + MMU, and the change_mask legacy behavior.

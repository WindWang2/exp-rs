# ADR 0105: Code-Review Remediation (qt-cpp-review pass over session slices)

## Context

The F-phase review loop ran the `qt-cpp-review` skill (deterministic lint + 6
parallel deep-analysis agents) over the session's newest files. Lint found 15
mechanical items; the agents surfaced several **real defects**, not style
nits — most importantly a dangling-owner crash path and an unbounded
allocation driven by raster content.

## Decision (all high-confidence findings fixed)

**Ownership & lifecycle**
- `RsRoiSpectrumTool` no longer deletes itself: `finishPolygon()` reports
  through the callback on **every** exit (empty values + an error message on
  failure) and the main window callback is the sole owner (always restores
  the map tool and `release()->deleteLater()`). This eliminates the double
  free when the tool self-deleted on its failure paths while the window's
  `unique_ptr` still owned it. A `m_finished` guard also kills the
  right-click double-fire (press + release both called `finishPolygon()`).
  The raster layer is held via `QPointer` (null-safe if removed mid-draw).

**Error handling & validation**
- `rs:post_classification_change`: auto-derived `classCount` is now validated
  against the UInt16 change-code ceiling **after** pass 1 and before the
  matrix allocation — a stray large band value can no longer OOM the
  `classCount²` matrix or corrupt change codes; `class_count < 0` is
  rejected. Both `rs:apply_mask` and `rs:post_classification_change` check
  `CPLGetLastErrorType()` after `out.close()` so deferred flush/trailer
  errors surface as failures instead of false success. `rs:apply_mask` maps
  data-content failures (grid, NoData, empty bands) to `InvalidInputData`,
  matching its sibling operator's error taxonomy.
- `BandRoleCombo` logs the open failure instead of silently degrading to
  "auto"; the ROI tool surfaces `meanSpectrum()`'s error in the status bar.

**Performance & correctness**
- `SpectralRoiProfile::meanSpectrum` reads each band's ROI window once
  (band-major `GDALRasterIO`) instead of 1×1 per pixel — O(bands) I/O calls
  instead of O(pixels × bands); dead buffer removed.
- `SpectralProfileWidget::setSpectrum` now computes the value range (the ROI
  mean curve previously never drew — zero range); the CR view caches its
  transform (recomputed on data/toggle change, not per paint); `drawLine` and
  `drawAxes` share one `plotRange()` and one `formatValue()` so the CR axis
  and value labels describe the plotted curve; value labels reuse the
  wavelength-scaled X fractions so they stay above their markers.
- `rs:post_classification_change` hoists the pass-2 code buffer out of the
  block loop. Lint HDR-3 `(std::min/max)` Windows-macro safety applied
  across the three files.

## Consequences

- The reviewed files are lint-clean except one verified-benign PAT-7
  (read-only iteration of `QgsProject::mapLayers()`'s by-value map).
- Regression coverage added for the OOM-class bug (auto class count above the
  UInt16 limit is rejected with an actionable error); the affected suites
  (spectral ROI 35/2, profile widget 91/9, band-role combo 19/1, operator
  1167/38) stay green.

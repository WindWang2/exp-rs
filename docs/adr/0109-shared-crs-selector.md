# ADR 0109: Shared CRS Selector Widget (C5)

## Context

The C5 task-center UI list calls for shared widgets for "CRS" alongside the
band-role (ADR 0102) and raster-selection (ADR 0107) helpers. The
orthorectification dialog used a bare `QLineEdit` for the target CRS with no
browse-to-picker path, and any future reprojection dialog would repeat it.

## Decision

- New `CrsSelector` (`app/widgets/crs_selector.{h,cpp}`): a line edit plus a
  browse button that opens the QGIS projection-selection dialog (fills the
  authid, falling back to WKT). Exposes `crsString()` / `setCrsString()`,
  `crs()` (parsed, `createFromUserInput` on the vendored QGIS), `isValid()`,
  and a `crsChanged` signal. The inner line edit stays reachable via
  `lineEdit()` so dialogs can name/style it and tests can find it by name.
- `OrthorectificationDialog` adopts it for the target-CRS input (the inner
  edit keeps the `orthoTargetCrsEdit` object name, so the existing dialog
  test is unchanged); `buildParams()` reads `crsString()`.

## Consequences

- The C5 shared-widget set is now complete for raster selection, band-role
  selection, and CRS; a future reproject dialog picks it up for free.
- The widget seam is pinned headlessly (authid round trip, garbage rejection,
  empty-invalid, crsChanged on edit — 12/1); the orthorectification dialog
  suite stays green (29/1).

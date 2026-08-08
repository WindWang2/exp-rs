# ADR 0103: Spectral Index Dialog Adopts the Shared Band-Role Combo

## Context

ADR 0102 introduced the shared `BandRoleCombo`, but the spectral index dialog
still carried its own band-role machinery: `readBandRoles()` (a
`GdalDatasetWrapper` role map) plus `comboIndexForRole()` and a bespoke
populate loop — the second copy of the same logic the QA-mask dialog had just
given up.

## Decision

- `SpectralIndexDialog`'s five band pickers (NIR/Red/Green/Blue/SWIR) become
  `BandRoleCombo` instances (with stable object names). `populateBandCombos()`
  now calls `setRaster()` per combo and expresses the whole selection policy
  with `selectBandByRole()` + a positional fallback for plain rasters:
  - role rasters: NIR/Red/Green/Blue by role; SWIR1 then SWIR2 for the
    SWIR picker;
  - plain rasters: the legacy Landsat/Sentinel positional mapping
    (bands 4/3/2/1, SWIR 5), converted for the combo's auto-item-at-index-0
    layout.
- `readBandRoles()` / `comboIndexForRole()` are deleted; the dialog's
  `QComboBox` members become `BandRoleCombo*`.
- Behavior parity is pinned by a headless dialog test: a 5-band product raster
  preselects bands 4/3/2/1 + SWIR 5 by role (labels carry the role names),
  and a plain 5-band raster gets the same positional bands.

## Consequences

- The third band-role reader disappears; every product-aware band picker now
  uses one shared widget (C5), and the role-selection policy is expressed in
  ~20 lines of declarative calls instead of ~50 lines of map/loop plumbing.
- The dialog test target reuses the standard base-class/helper link set and
  stays green alongside the QA-mask and widget suites.

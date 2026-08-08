# ADR 0102: Shared Band-Role Selector Widget (C5)

## Context

The C5 task-center UI requirement calls for shared widgets instead of one-off
dialog behavior: "Use shared widgets/helpers for ... band-role selection".
Two product-aware dialogs (QA mask, spectral index) each implemented their own
`SICNU_BAND_ROLE` reading and labeled band-combo population, duplicating the
same role-resolution logic.

## Decision

- New `BandRoleCombo` (`app/widgets/band_role_combo.{h,cpp}`): a QComboBox
  that populates a raster's bands labeled with their semantic role
  (`波段 N (NIR)`, ...) plus an "自动（按产品语义角色）" item, and offers
  `selectedBand()` (1-based, 0 = auto), `selectedRole()`, and
  `selectBandByRole()` (falls back to auto). Band numbers remain available as
  the low-level fallback (ADR-0065 principle).
- `QaMaskDialog` now uses `BandRoleCombo` for its QA-band picker: its bespoke
  `populateBandCombo()` and role-index helpers are deleted; the SCL→QA
  preselection is expressed as two `selectBandByRole()` calls. The operator
  JSON contract is unchanged (auto → no `qa_band`).

## Consequences

- One canonical band-role picker for future dialogs (spectral index
  adoption is a follow-up); the QA-mask dialog loses ~45 lines of duplicated
  role logic while keeping identical behavior.
- The widget seam is tested headlessly (role labels, select-by-role with
  fallback, unreadable-source clearing); the QA-mask kernel suite stays green
  (72/5) and the app builds clean.

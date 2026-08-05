# ADR 0040: Deepen QgisDisplayManager Layer Tree & Visibility Seams

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`QgisDisplayManager` managed presentation instances and Data Asset leases, but callers adjusting layer visibility or tree Z-ordering had to access QGIS `QgsLayerTree` nodes directly outside manager seams.

## Decision

1. **Visibility Seam**: Expose `setLayerVisible(DisplayLayerId, bool)` and `isLayerVisible(DisplayLayerId) const` on `QgisDisplayManager`.
2. **Tree Ordering Seams**: Expose `moveLayerTop(DisplayLayerId)` and `moveLayerBottom(DisplayLayerId)` to manage layer node positions in the associated `QgsLayerTree`.

## Consequences

- **Encapsulation**: Callers operate strictly on `DisplayLayerId` handles without manipulating raw `QgsLayerTree` or `QgsLayerTreeNode` pointers.
- **Maintainability**: Layer visibility and order changes stay localized within `QgisDisplayManager`.

# ADR 0095: Reusable Align-and-Compare Change Detection DAG

## Context

The mission's E-phase asks for reusable change-detection DAGs, and the DoD
workflow requires "Align two acquisitions → run change detection" through the
TaskCenter seam. The pieces existed (gdal:reproject `reference` alignment,
ADR 0091; rs:change_detection; the TaskPanel DAG host), but the only reusable
change workflow was the atomic `tool.rs.change_detection` single step, which
demanded pre-aligned inputs and left grid harmonization to the user.

## Decision

Register `lab.change.align_difference` ("变化检测（自动对齐）") as a compound
TaskPanel workflow over the existing operator registry:

- `align_before` — `gdal:reproject` with the after-date raster as
  `reference` (grid harmonization, nearest resampling), producing an
  `aligned` artifact;
- `change` — `rs:change_detection` difference consuming
  `$align_before.aligned` via the artifact placeholder (ADR 0016) and the
  user-supplied `after` raster; gated on `paramNonEmpty:change.after`;
  the change map is added to the map canvas on success.

No new algorithm code: the DAG orchestrates the same Processing Registry
operators the GUI, CLI, and Agent use.

## Consequences

- The full align-then-compare sequence is now one reusable TaskCenter
  workflow: users pick two rasters and the workflow harmonizes grids before
  differencing — no manual CRS/resolution transcription.
- A workflow-runtime test pins the topology (2 steps, operator ids, the
  `$align_before.aligned` flow, and the `change.after` gate); the builtin
  definition count is kept in sync.

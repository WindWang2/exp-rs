# ADR 0059: Delete the Vendored Vector Warper Cluster; Add Helmert/Projective Numeric Tests

## Status
Accepted

## Context
`QgsVectorWarper` (138+166 lines) and `QgsGcpGeometryTransformer` (97+64
lines) were vendored from QGIS for upstream-API parity but have zero
production callers — only their own tests use them; the geometry transformer
is a ~40-line pass-through to `QgsAbstractGeometryTransformer` (from qgis_core),
mirroring the ADR 0020 S3 deletion of the pass-through `QgsGCPList`. Meanwhile
`QgsLeastSquares::helmert` and `::projective` had no numeric coverage, and were
untestable anyway: no CMake code set `HAVE_GSL`, so both always threw
`QgsNotSupportedException`.

## Decision
1. **Delete the warper cluster**: `qgsvectorwarper.h/.cpp`,
   `qgsgcpgeometrytransformer.h/.cpp`, `tests/test_vector_warper.cpp`,
   `tests/test_gcp_geometry_transformer.cpp`, and their CMake entries.
   Dead vendored surface is not a goal.
2. **Wire GSL into the build** (`find_package(GSL)` + `GSL::gsl` on
   `qgis_analysis`, `HAVE_GSL` in `qgsconfig.h`) so both fits become live.
3. **Add numeric tests**: helmert (known rotation+scale+translation recovery,
   singular input → exception) and projective (known homography recovery,
   degenerate input) in `test_least_squares.cpp`; Helmert/Projective
   round-trips plus invertYAxis cases — including ports of the upstream QGIS
   reference values — in `test_gcp_transformer.cpp`.

## Consequences
- **~400 lines of dead vendored code deleted**; only the actively-used
  transformer surface remains in the georeferencing seam.
- **Helmert/projective math is pinned correct** against by-construction
  ground truth and upstream literals (tolerance 1), resolving the author's
  "derived it myself late at night" doubt.
- **GSL becomes a real build dependency**, matching upstream QGIS.

# Multidimensional Data Guide

Variable/time/level/x/y semantics for NetCDF (and, driver-permitting, HDF5 and
Zarr), through the lazy view in `src/geospatial/multidim/multidim_view.h`.

## Contract

1. **Listing is lazy** — opening and enumerating variables/dimensions reads no
   array data (`inspectMultidim`, `MultidimView::metadata()`).
2. **Slices address dimensions by name** — every non-spatial dimension must be
   fixed by name (`{"time", 0}`), leaving exactly two free dimensions (rows ×
   cols). Partial slices and unknown names are structured errors.
3. **No flatten-to-bands** — remaining un-sliced dimensions are NEVER folded
   into pseudo-bands or extra rows; that silently changes data semantics and is
   forbidden by contract (`readSlice` throws instead).
4. **Stored values, declared nodata** — slice buffers are stored values in
   row-major order with the variable's declared nodata/scale/offset carried
   alongside; apply scale/offset explicitly when physical values are needed.

```cpp
sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open("cube.nc");
// enumerate
for (const auto &var : view.metadata().variables) { /* name, dims, unit … */ }
// time slice (fixed 3×4 grid)
auto grid = view.readTemporalOrLevelSlice("sst", "time", 1);
// named slice form
auto band = view.readSlice("sst", {{"time", 1}, {"y", 2}});
```

## Format support

| Driver | Read | Slice export | Notes |
|---|---|---|---|
| netCDF | ✔ | ✔ (slice → GeoTIFF certified) | CF conventions: dimension types (TEMPORAL / HORIZONTAL_X/Y) and indexing-variable units surface in metadata |
| HDF5 | Accessible | via GDAL subdatasets | fails closed when driver absent |
| Zarr | Accessible | — | offered when the driver is present; certification pending |

Driver-gated cases in `tests/test_io_multidim.cpp` self-skip **with a logged
reason** when the driver is missing — never silently.

## Slice → GeoTIFF

```cpp
auto slice = view.readTemporalOrLevelSlice("temperature", "time", t);
sicnu::geo::RasterWriter w = sicnu::geo::RasterWriter::create(
    target, slice.cols, slice.rows, { spec }, {});
w.writeWindow(1, {0, 0, slice.cols, slice.rows}, slice.values.data());
w.finalize(); // atomic publish with declared nodata/metadata
```

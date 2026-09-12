# PERFORMANCE — resource bounds evidence

Host: 16 cores / 62 GiB RAM, clang 22.1.8, Debug builds (numbers are NOT
comparable with the Release benchmarks in `benchmarks/`; grades documented
per measurement).

## M0/M1 changes — complexity unchanged, work reduced

- `fillDepressions` (#848): seeding now also scans the 8-neighbourhood of
  each interior valid cell once — O(8N) extra worst case; on NoData-free
  rasters the scan is skipped entirely (perimeter check first). Targeted
  benchmark (`-O2`, clang 22.1.8, 2000×2000 = 4 M cells, min of 3 reps,
  host shared with parallel build activity — treat deltas < ~30 % as noise):
  - NoData-free DEM: baseline `132da5e998` 2283 ms vs fixed 2038 ms —
    no measurable cost from the seeding change.
  - 20 % scattered NoData: baseline 960 ms vs fixed 3087 ms. The
    baseline number is the defect working as designed-badly: with only
    rim seeds the queue stayed nearly empty on such rasters and the
    function returned having filled nothing. The added time is the flood
    actually running (plus the boundary scans); the contract stays
    O(N log N) time / O(N) memory.
- `isDirNoData` / `watershedLabels` (#853): two extra float compares per
  value; no allocation, no complexity change.
- SAR Horn (#855): two scalars instead of one — no measurable arithmetic
  delta (dominated by DEM I/O).
- Spectral scale probe (#856): the removed dead block used to re-read 5
  windows × 2 bands per run (up to 5·2·64·64 = 40960 samples) purely to
  compute an unread flag — deletion is a small net I/O win; the surviving
  probe is unchanged (bounded 4×4 windows of 32×32 = ≤16 KiB samples,
  resolved once per raster).
- Per-band NoData (#854): two GDAL calls instead of one at output setup;
  nothing on the tile path.
- `domainFromDeclaredScale` (#873): one `std::isfinite` added.

## Standing contracts (unchanged, audited)

- Streaming operators remain O(block) resident (`GdalStreamingOutput`,
  block reads ≤ 256 rows); no M0/M1 change touches a streaming path's
  memory class.
- The flatten operator's `executionEstimate` (6·256²·4 B) still bounds its
  tile buffers.

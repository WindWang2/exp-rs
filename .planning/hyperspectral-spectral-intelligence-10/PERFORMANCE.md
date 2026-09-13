# PERFORMANCE — hyperspectral-spectral-intelligence-10

Numbers are evidence, not gates (no wall-clock regression gates). Record
method + machine context per measurement.

## Baseline observations (read from code, to verify in Phase 5)

* `rs:mnf` declares FullRaster, ~4x raster bytes (`rs_mnf_operator.cpp:52-61`) — replaced by streaming WP-D kernel with O(tile·B + B²) working set.
* PPI streaming already O(tile·B + proj·B + pixels·4B) (`rs_endmember_extraction_operator.cpp:62-73`).
* MF/ACE/RX: three-pass streaming, O(tile + B²).
* Unmixing: 512² tile streaming.

## This track's scale targets

* MNF forward+inverse on a synthetic 256-band logical cube (small extents,
  band count is the scale axis) — memory profile asserted via design (streaming
  accumulators), timing recorded as evidence only.
* Spectral-table I/O at the size bound — refusal path verified without
  materializing oversized JSON in tests beyond the bound probe.
* FCLS per-pixel cost documented (E×E NNLS); scaling table for E = 4, 8, 16.

# R27-T04 Bench Baseline — GUI-slot responsiveness (#1790/#1791/#1794)

Binary: `tests/test_r27_t04_async_bench` (offscreen, -j1 host under load average ~3–8).
Methodology: per path, the measured **slot cost** is the wall time of the GUI-thread
entry point (dispatch + snapshot only, post-fix); the **e2e** latency is the same
call's full completion measured from dispatch to landing on the GUI thread. On HEAD
(10652fd003) the same entry point performed the whole computation synchronously
inside the slot — the identical work measured by e2e today is therefore the HEAD
slot-cost lower bound (same data, same machine, same GDAL build); no HEAD binary
was rebuilt for this table and no HEAD number below is invented beyond that
identity.

Two consecutive full runs (both green, thresholds slot p95 < 50 ms):

| Path | Workload | slot p50 (run1/run2) | slot p95 (run1/run2) | e2e (run1/run2) | HEAD slot cost (identity) |
|---|---|---|---|---|---|
| recomputeSpectralCurves (#1794) | 1200×1200×3, 4 classes × 10 ROI samples | 1.119 / 1.091 ms | 1.158 / 1.209 ms | 18–22 ms first landing | ≥ e2e (≈20 ms for this ROI set; grows with raster/ROI count — unbounded on the GUI thread at HEAD) |
| CrossSectionWidget::extractProfile (#1790) | 1100-sample transect, 1200×1200×3 | 0.008 / 0.009 ms | 0.012 / 0.010 ms | 4.3 ms p95 | ≥ 4.3 ms (1100 per-sample RasterIO calls inline) |
| RsRoiToolMagicWand::canvasReleaseEvent (#1791) | 513×513×3 window read + flood fill + contour | 0.014 / 0.014 ms | 0.014 / 0.015 ms | 362.5 / 361.9 ms p95 | ≥ 362 ms inline in the map-tool release event |

Reading: the GUI thread no longer blocks on any of the three paths — slot p95
≤ 1.21 ms while the full computation (18–362 ms for these workloads) runs on the
bounded scan pool; a remote/slow source extends only the worker time, not the
slot. Assertions pin slot p95 < 50 ms to catch a regression back to synchronous
behavior.

Event-loop responsiveness during in-flight work is structurally guaranteed (work
off-thread; cancellation via RsScanPool generations; superseded landings dropped)
and exercised functionally by the async test suites (re-invoke storms, supersede,
window destruction with in-flight reads — all green twice).

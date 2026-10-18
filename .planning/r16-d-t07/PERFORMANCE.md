# R16-D-T07 Performance Notes

No benchmark numbers are claimed. The perf-class changes that landed are behavior-preserving by construction; observations only.

## Landed (correctness/perf, unmeasured — noted honestly)
- WP-7: three `provider->bandStatistics` calls that ran uncapped full-band scans per Apply on the GUI thread now pass `sampleSize=250000`, matching the #634 sampling pattern used by bandRange/histograms. Effect: bounded first-touch cost per band instead of full raster scan.
- WP-7: piecewise `enhance()` O(n) linear scan → O(log n) `std::lower_bound` per value (n ≈ 257 for gamma/equalization curves; QGIS builds LUTs only for integer dtypes, float rasters pay per-pixel).
- WP-4: band-combo/slider mutation paths no longer trigger repaints of an unintended fallback raster (avoids spurious full-canvas renders when the current layer is not a raster).

## Observed, unchanged (audit quantified by code reading)
- T07-F08 magic-wand full-plane read (classification_studio_widget readLayerPlanes): 10000×10000×7 float ≈ 2.8 GB before the bounded BFS — latent (no production canvas hook); deferred with evidence in IMPLEMENTATION.md.
- Event-storm candidates were audited and REJECTED (history/temporal panels already coalesce; see audit's rejected list in ../r16-d/WORK_PACKAGES.md).

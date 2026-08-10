# Design & Architecture Decisions

## Decision Log

### AD-001: Autonomous Strategy & Standards
- All performance optimizations must be backed by empirical benchmarks.
- No reduction of output quality, floating-point precision, or nodata checks.
- Zero-copy buffer reuse and GDAL block-aligned I/O prioritized for streaming.
- Global statistics algorithms (PCA, Gram-Schmidt) strictly follow 2-pass streaming semantics to eliminate full-frame in-memory arrays.

### AD-002: Image Fusion Out-of-Core Tile Streaming
- Standardized default block size to 512x512 pixels.
- Refactored `linear`, `brovey`, and `ihs` algorithms in `processNativeFusion` to stream 512x512 windows via GDAL `readBandWindow` and `writeBandWindow`.
- For `brovey`, eliminated full-frame `msSum` array; pixel sums are computed on-the-fly inside tile buffers.
- For `ihs`, implemented 2-pass tile streaming (Pass 1 computes Intensity and Pan global mean/stddev, Pass 2 streams block match and writes output GeoTIFF).
- Memory policy for `RsImageFusionOperator` updated to `RSOperatorMemoryPolicy::Streaming` with a fixed 12 MiB tile working set resource estimate.

### AD-003: High-Risk Operators Out-of-Core 2-Pass Refactoring (PCA, Spectral Unmixing, RX Anomaly)
- Refactored `ImageEnhancement::processPcaFile`: Pass 1 streams 512x512 tiles to compute online band means and $N \times N$ covariance matrix; Pass 2 streams block forward projections to GeoTIFF.
- Refactored `RsSpectralUnmixingOperator`: process 512x512 tile windows, calling least-squares unmixing on per-tile pixel arrays and writing abundance bands directly with `writeBandWindow`.
- Refactored `RsRxAnomalyOperator`: Pass 1 accumulates 512x512 tile statistics for background covariance inversion; Pass 2 streams Mahalanobis distance scores to single-band GeoTIFF.
- RAM footprint for all refactored operators drops from $O(\text{width} \times \text{height} \times \text{bands})$ to $O(\text{tileWidth} \times \text{tileHeight} \times \text{bands}) \approx 12 \text{ MiB}$ fixed working set.

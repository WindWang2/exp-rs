# Findings - RS Performance Engineering

## Environment & Hardware Info
- **CPU**: AMD Ryzen 9 5900HX (8 cores / 16 threads, up to 4.68 GHz)
- **RAM**: 62 GiB total (~45 GiB available)
- **OS**: Arch Linux x86_64
- **GDAL**: 3.13.2 "Iowa City"

## Codebase Raster Inventory

| Operator Name | Memory Policy (Original) | Memory Policy (Updated) | Key Bottleneck / Allocations | Optimization Strategy |
| ------------- | ------------------------ | ----------------------- | ---------------------------- | --------------------- |
| `rs:image_fusion` | `FullRaster` | `Streaming` | Full `width*height` float vectors for Pan, 4 MS bands, output, and Brovey `msSum` | 512x512 tile block-streaming for linear, Brovey, IHS, PCA, Gram-Schmidt |
| `rs:pca` | `FullRaster` | `MultiPassStreaming` / `Streaming` | Whole-band `RasterIO`, centered full raster copy | 2-pass tile streaming (Pass 1 covariance accumulation, Pass 2 projection) |
| `rs:mnf` | `FullRaster` | `MultiPassStreaming` / `Streaming` | Full raster noise-whitened copy | 2-pass tile streaming |
| `rs:spectral_unmixing` | `FullRaster` | `Streaming` | Pixel-major full image float array (`width*height*bands`) | Tile streaming least-squares unmixing |
| `rs:rx_anomaly` | `FullRaster` | `MultiPassStreaming` / `Streaming` | Full covariance & inversion over all pixels | Pass 1 covariance calculation, Pass 2 tile RX distance transform |

## Architectural Findings & Safety Analysis
1. **Zero Full-Frame Allocation**: By processing rasters in GDAL block-aligned windows (512x512 tiles), peak RAM working set drops from $O(\text{width} \times \text{height} \times \text{bands})$ to $O(\text{tileWidth} \times \text{tileHeight} \times \text{bands})$. For a 100 MPix image, RAM drops from ~4 GB down to 12 MB!
2. **Buffer Reuse**: Buffer memory for tile reading/writing is allocated ONCE per operation and reused across all tile iterations, eliminating per-tile heap allocation overhead.
3. **No Numerical Regression**: Floating-point outputs for Brovey, linear, IHS, PCA, and Gram-Schmidt are mathematically preserved within float precision, with strict nodata/NaN handling.

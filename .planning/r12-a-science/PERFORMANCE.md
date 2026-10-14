# Performance Benchmarks & Convergence — R12-A Science Numerics

## Benchmark Baseline & Monitoring
- **Hardware Profile**: Linux, 2-core compiler cap.
- **Metrics Tracked**:
  - Multiresolution segmentation execution time (ms) and peak RSS (MB).
  - Classification pipeline predict time (ms) across Byte, UInt16, Int32 targets.
  - Spectral unmixing / FCLS solver convergence iteration count and RMSE.
- **Target Convergence**:
  - No regression exceeding 5% in runtime latency for standard test datasets.
  - Zero memory leaks across repeat runs.

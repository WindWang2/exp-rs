# Performance Observatory — Direction D

## Resource Budget & Hard Limits
- Build concurrency: Strict `-j2` (`CMAKE_BUILD_PARALLEL_LEVEL=2`).
- Test execution: Strict `-j1` (`CTEST_PARALLEL_LEVEL=1`).
- Platform: Headless Linux (`QT_QPA_PLATFORM=offscreen`).

## Baseline Metrics
- To be benchmarked across UI matrix table updates and terminology loads.

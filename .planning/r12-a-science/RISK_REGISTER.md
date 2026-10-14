# Risk Register — R12-A Science Numerics Hardening

## Identified Risks & Mitigations

| Risk ID | Description | Severity | Probability | Mitigation Strategy |
|---|---|---|---|---|
| R-01 | Concurrent build contention with other worktrees | High | Medium | Use OS-level flock `/tmp/exp-rs-build-j2.lock` and enforce `-j2` build limit. |
| R-02 | Memory exhaustion during heavy Catch2 test runs | High | Low | Run tests sequentially (`-j1`); throttle parallel allocations. |
| R-03 | Breaking existing tests by changing GeoTIFF dtypes | Medium | Low | Ensure existing class ranges (0-255) remain GDT_Byte; widen only when class IDs exceed uint8 or minClassId < 0. |
| R-04 | CRS transform failure breaking legitimate tests | Medium | Low | Only reject when `OGR_G_Transform` actually returns failure (`OGRERR_FAILURE`); do not reject identity transforms. |
| R-05 | File descriptor or resource leaks in temporary files | Low | Low | Use RAII wrappers for GDAL datasets and atomic file handles. |

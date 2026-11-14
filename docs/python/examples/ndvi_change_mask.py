"""Agent Python task: aligned NDVI rasters -> Byte change mask and statistics.

The host supplies inputs, output, parameters and result globals.
0 = unchanged, 1 = changed, 255 = invalid in either observation.
"""
import numpy as np
import rasterio

if len(inputs) != 2:
    raise ValueError("NDVI change requires exactly two input rasters")
threshold = float(parameters.get("threshold", 0.2))
if not np.isfinite(threshold) or threshold < 0:
    raise ValueError("threshold must be finite and non-negative")

with rasterio.open(inputs[0]) as before, rasterio.open(inputs[1]) as after:
    if (before.count != 1 or after.count != 1 or
            before.shape != after.shape or before.crs != after.crs or
            before.transform != after.transform):
        raise ValueError("NDVI inputs must be single-band rasters on the same CRS and grid")
    profile = before.profile.copy()
    profile.update(driver="GTiff", dtype="uint8", count=1, nodata=255)
    valid_pixels = changed_pixels = 0
    with rasterio.open(output, "w", **profile) as destination:
        for _, window in before.block_windows(1):
            first = before.read(1, window=window, masked=True)
            second = after.read(1, window=window, masked=True)
            valid = (~np.ma.getmaskarray(first) & ~np.ma.getmaskarray(second)
                     & np.isfinite(first.data) & np.isfinite(second.data))
            changed = valid & (np.abs(second.data - first.data) > threshold)
            mask = np.full(first.shape, 255, dtype=np.uint8)
            mask[valid] = changed[valid].astype(np.uint8)
            destination.write(mask, 1, window=window)
            valid_pixels += int(np.count_nonzero(valid))
            changed_pixels += int(np.count_nonzero(changed))

result.update(changed_pixels=changed_pixels, valid_pixels=valid_pixels,
              changed_fraction=(changed_pixels / valid_pixels if valid_pixels else None))

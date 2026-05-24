"""Python fallback implementations for raster_ops C++ functions.

Used when the compiled C++ extension is not available or missing functions.
All functions use numpy for computation.
"""

import numpy as np


def stretch_and_compose_rgb(r, g, b, r_min, r_max, g_min, g_max, b_min, b_max):
    """Stretch three bands to [0,255] and compose into RGB uint8 array."""
    def stretch(arr, amin, amax):
        if amax - amin > 0:
            return np.clip((arr - amin) / (amax - amin) * 255.0, 0, 255).astype(np.uint8)
        return np.zeros_like(arr, dtype=np.uint8)

    r8 = stretch(r, r_min, r_max)
    g8 = stretch(g, g_min, g_max)
    b8 = stretch(b, b_min, b_max)
    h, w = r8.shape
    rgb = np.stack([r8, g8, b8], axis=-1)
    return np.ascontiguousarray(rgb)


def _affine_warp(band, out_w, out_h, coeffs_x, coeffs_y):
    """Warp a 2D band using 6 affine coefficients [a0, a1, a2].

    coeffs_x = [a0, a1, a2]: src_x = a0 + a1*col + a2*row
    coeffs_y = [b0, b1, b2]: src_y = b0 + b1*col + b2*row
    """
    h_src, w_src = band.shape
    a0, a1, a2 = float(coeffs_x[0]), float(coeffs_x[1]), float(coeffs_x[2])
    b0, b1, b2 = float(coeffs_y[0]), float(coeffs_y[1]), float(coeffs_y[2])

    # Vectorized: build source coordinate grids for all output pixels
    rows = np.arange(out_h, dtype=np.float64)
    cols = np.arange(out_w, dtype=np.float64)
    col_grid, row_grid = np.meshgrid(cols, rows)

    src_x = a0 + a1 * col_grid + a2 * row_grid
    src_y = b0 + b1 * col_grid + b2 * row_grid

    # Integer parts and fractional parts for bilinear interpolation
    sx = np.floor(src_x).astype(np.int32)
    sy = np.floor(src_y).astype(np.int32)
    fx = (src_x - sx).astype(np.float32)
    fy = (src_y - sy).astype(np.float32)

    # Mask valid pixels
    valid = (sx >= 0) & (sx < w_src - 1) & (sy >= 0) & (sy < h_src - 1)

    out = np.zeros((out_h, out_w), dtype=np.float32)

    # Clamp indices for safe gathering (masked invalids won't be used)
    sx_safe = np.clip(sx, 0, w_src - 2)
    sy_safe = np.clip(sy, 0, h_src - 2)

    # Gather 4 neighbors
    v00 = band[sy_safe, sx_safe]
    v10 = band[sy_safe, sx_safe + 1]
    v01 = band[sy_safe + 1, sx_safe]
    v11 = band[sy_safe + 1, sx_safe + 1]

    # Bilinear interpolation
    result = (
        v00 * (1 - fx) * (1 - fy) +
        v10 * fx * (1 - fy) +
        v01 * (1 - fx) * fy +
        v11 * fx * fy
    )

    out[valid] = result[valid]
    return out


def warp_and_compose_rgb(r, g, b, out_w, out_h, coeffs_x, coeffs_y,
                          r_min, r_max, g_min, g_max, b_min, b_max):
    """Warp three bands using affine coefficients, stretch, and compose RGB."""
    r_warped = _affine_warp(r.astype(np.float32), out_w, out_h, coeffs_x, coeffs_y)
    g_warped = _affine_warp(g.astype(np.float32), out_w, out_h, coeffs_x, coeffs_y)
    b_warped = _affine_warp(b.astype(np.float32), out_w, out_h, coeffs_x, coeffs_y)
    return stretch_and_compose_rgb(r_warped, g_warped, b_warped,
                                    r_min, r_max, g_min, g_max, b_min, b_max)


def stretch_gray(arr, amin, amax):
    """Stretch a single band to grayscale RGB uint8 array."""
    if amax - amin > 0:
        gray = np.clip((arr - amin) / (amax - amin) * 255.0, 0, 255).astype(np.uint8)
    else:
        gray = np.zeros_like(arr, dtype=np.uint8)
    h, w = gray.shape
    rgb = np.stack([gray, gray, gray], axis=-1)
    return np.ascontiguousarray(rgb)


def warp_and_stretch_gray(band, out_w, out_h, coeffs_x, coeffs_y, amin, amax):
    """Warp a single band and stretch to grayscale RGB."""
    warped = _affine_warp(band.astype(np.float32), out_w, out_h, coeffs_x, coeffs_y)
    return stretch_gray(warped, amin, amax)


def warp_raster_band(band, out_w, out_h, coeffs_x, coeffs_y):
    """Warp a single band using affine coefficients."""
    return _affine_warp(band.astype(np.float32), out_w, out_h, coeffs_x, coeffs_y)

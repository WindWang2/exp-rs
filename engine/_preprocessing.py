import numpy as np
import rasterio
import os
from engine.registry import register_tool

def calculate_polynomial_coeffs(src_pts, dst_pts, order=1):
    num_pts = src_pts.shape[0]
    if order == 1:
        A = np.column_stack([np.ones(num_pts), src_pts[:, 0], src_pts[:, 1]])
    else:
        A = np.column_stack([
            np.ones(num_pts), src_pts[:, 0], src_pts[:, 1],
            src_pts[:, 0]**2, src_pts[:, 0]*src_pts[:, 1], src_pts[:, 1]**2
        ])
    
    coeffs_x, _, _, _ = np.linalg.lstsq(A, dst_pts[:, 0], rcond=None)
    coeffs_y, _, _, _ = np.linalg.lstsq(A, dst_pts[:, 1], rcond=None)
    return coeffs_x, coeffs_y

import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../build'))

def pca_pansharpen_arrays(ms_bands, pan_band):
    """
    ms_bands: shape (bands, height, width)
    pan_band: shape (height, width)
    """
    import raster_ops
    bands, h, w = ms_bands.shape
    
    # Flatten to (pixels, bands)
    ms_flat = ms_bands.reshape(bands, -1).T.astype(np.float32)
    
    # Forward PCA
    projected, evecs, mean = raster_ops.compute_pca(ms_flat)
    
    pan_flat = pan_band.reshape(-1).astype(np.float32)
    
    # Histogram matching PAN to PC1
    pan_mean = pan_flat.mean()
    pan_std = pan_flat.std()
    pc1_mean = projected[:, 0].mean()
    pc1_std = projected[:, 0].std()
    
    if pan_std != 0:
        pan_matched = (pan_flat - pan_mean) * (pc1_std / pan_std) + pc1_mean
    else:
        pan_matched = pan_flat
        
    projected[:, 0] = pan_matched
    
    # Inverse PCA: data = projected * evecs.T + mean
    inversed = np.dot(projected, evecs.T) + mean
    
    # Reshape back
    sharpened = inversed.T.reshape(bands, h, w)
    return sharpened.astype(ms_bands.dtype)


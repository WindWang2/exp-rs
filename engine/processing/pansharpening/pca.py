# engine/processing/pansharpening/pca.py
import numpy as np
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../../build'))
from engine.registry import register_tool

@register_tool(
    name="pca_pansharpen",
    label="PCA Pansharpening",
    category="Processing",
    description="Pansharpening using Principal Component Analysis",
    params=[
        {"name": "ms_bands", "label": "Multi-spectral Bands", "type": "array", "required": True, "help": "Multi-spectral band array (bands, height, width)"},
        {"name": "pan_band", "label": "Panchromatic Band", "type": "array", "required": True, "help": "Panchromatic band array (height, width)"}
    ]
)
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

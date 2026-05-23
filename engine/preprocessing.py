import numpy as np
import rasterio
import os
from engine.registry import register_tool

def calculate_dos1_band(band_data, nodata=None, dark_value=None):
    if dark_value is None:
        if nodata is not None:
            masked_data = np.ma.masked_equal(band_data, nodata)
            if masked_data.count() == 0:
                return band_data
            dark_value = masked_data.min()
        else:
            dark_value = np.min(band_data)
        
    if np.issubdtype(band_data.dtype, np.floating):
        dtype_max = np.finfo(band_data.dtype).max
    else:
        dtype_max = np.iinfo(band_data.dtype).max
        
    corrected = band_data.astype(np.float32) - dark_value
    result = np.clip(corrected, 0, dtype_max).astype(band_data.dtype)
    if nodata is not None:
        result[band_data == nodata] = nodata
    return result

@register_tool(
    name="dos1_correction",
    label="DOS1 Atmospheric Correction",
    category="Preprocessing",
    description="Performs Dark Object Subtraction (DOS1) atmospheric correction on a raster image.",
    params=[
        {"name": "input_path", "label": "Input Raster", "type": "file"},
        {"name": "output_path", "label": "Output Raster", "type": "file"}
    ]
)
def calculate_dos1(input_path: str, output_path: str) -> str:
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)

    
    with rasterio.open(input_path) as src:
        profile = src.profile.copy()
        nodata = src.nodata
        
        # Calculate dark values for each band first (global minimum)
        dark_values = []
        for i in range(1, src.count + 1):
            band_min = None
            for _, window in src.block_windows():
                band_data = src.read(i, window=window)
                if nodata is not None:
                    masked = np.ma.masked_equal(band_data, nodata)
                    if masked.count() > 0:
                        curr_min = masked.min()
                        if band_min is None or curr_min < band_min:
                            band_min = curr_min
                else:
                    curr_min = np.min(band_data)
                    if band_min is None or curr_min < band_min:
                        band_min = curr_min
            dark_values.append(band_min if band_min is not None else 0)

        with rasterio.open(output_path, 'w', **profile) as dst:
            for _, window in src.block_windows():
                data = src.read(window=window)
                corrected_window = np.zeros_like(data)
                for i in range(src.count):
                    corrected_window[i] = calculate_dos1_band(data[i], nodata=nodata, dark_value=dark_values[i])
                
                dst.write(corrected_window, window=window)
                
    return output_path

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


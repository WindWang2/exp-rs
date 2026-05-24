import numpy as np
import rasterio
import os
from analysis.qgsprocessingregistry import register_tool

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

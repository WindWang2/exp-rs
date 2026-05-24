import os
import numpy as np
import rasterio
from core.qgsreader import GeospatialReader
from analysis.qgsprocessingregistry import register_tool

@register_tool(
    name="calculate_ndvi",
    label="Normalized Difference Vegetation Index (NDVI)",
    category="Raster Algebra",
    description="Calculates vegetation vigor (NDVI) from Red and NIR bands of a multi-spectral image.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-band remote sensing raster file"},
        {"name": "output_path", "label": "Output Raster File", "type": "file", "required": True, "help": "Path where the calculated NDVI GeoTIFF will be saved"},
        {"name": "red_band", "label": "Red Band Index", "type": "int", "default": 1, "required": True, "help": "Band index representing Red wavelength (1-indexed)"},
        {"name": "nir_band", "label": "NIR Band Index", "type": "int", "default": 2, "required": True, "help": "Band index representing Near-Infrared wavelength (1-indexed)"}
    ]
)
def calculate_ndvi(input_path: str, output_path: str, red_band: int = 1, nir_band: int = 2) -> str:
    """
    Calculates Normalized Difference Vegetation Index (NDVI).
    NDVI = (NIR - Red) / (NIR + Red)
    """
    reader = GeospatialReader(input_path)
    if not reader.is_raster:
        raise ValueError("Input file is not a raster dataset")
    
    red = reader.read_raster_band(red_band).astype(np.float32)
    nir = reader.read_raster_band(nir_band).astype(np.float32)
    
    # Safe vectorized division
    with np.errstate(divide='ignore', invalid='ignore'):
        ndvi = (nir - red) / (nir + red)
        ndvi = np.nan_to_num(ndvi, nan=0.0, posinf=1.0, neginf=-1.0)
    
    # Write output GeoTIFF keeping spatial profiles
    with rasterio.open(input_path) as src:
        profile = src.profile.copy()
        profile.update(
            driver="GTiff",
            count=1,
            dtype=rasterio.float32
        )
        
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with rasterio.open(output_path, "w", **profile) as dst:
        dst.write(ndvi, 1)
        
    return output_path


@register_tool(
    name="calculate_ndwi",
    label="Normalized Difference Water Index (NDWI)",
    category="Raster Algebra",
    description="Calculates open water body intensity (NDWI) from Green and NIR bands of a multi-spectral image.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-band remote sensing raster file"},
        {"name": "output_path", "label": "Output Raster File", "type": "file", "required": True, "help": "Path where the calculated NDWI GeoTIFF will be saved"},
        {"name": "green_band", "label": "Green Band Index", "type": "int", "default": 1, "required": True, "help": "Band index representing Green wavelength (1-indexed)"},
        {"name": "nir_band", "label": "NIR Band Index", "type": "int", "default": 2, "required": True, "help": "Band index representing Near-Infrared wavelength (1-indexed)"}
    ]
)
def calculate_ndwi(input_path: str, output_path: str, green_band: int = 1, nir_band: int = 2) -> str:
    """
    Calculates Normalized Difference Water Index (NDWI).
    NDWI = (Green - NIR) / (Green + NIR)
    """
    reader = GeospatialReader(input_path)
    if not reader.is_raster:
        raise ValueError("Input file is not a raster dataset")
    
    green = reader.read_raster_band(green_band).astype(np.float32)
    nir = reader.read_raster_band(nir_band).astype(np.float32)
    
    # Safe vectorized division
    with np.errstate(divide='ignore', invalid='ignore'):
        ndwi = (green - nir) / (green + nir)
        ndwi = np.nan_to_num(ndwi, nan=0.0, posinf=1.0, neginf=-1.0)
        
    with rasterio.open(input_path) as src:
        profile = src.profile.copy()
        profile.update(
            driver="GTiff",
            count=1,
            dtype=rasterio.float32
        )
        
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with rasterio.open(output_path, "w", **profile) as dst:
        dst.write(ndwi, 1)
        
    return output_path

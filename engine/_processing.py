import os
import numpy as np
import rasterio
from sklearn.cluster import KMeans
from .core.reader import GeospatialReader
from .registry import ToolRegistry
from ._preprocessing import calculate_dos1

# Initialize central registry
registry = ToolRegistry()

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

def kmeans_classify(input_path: str, output_path: str, bands: str = "1,2,3", clusters: int = 5) -> str:
    """
    Performs K-Means unsupervised land classification on specified bands.
    """
    reader = GeospatialReader(input_path)
    if not reader.is_raster:
        raise ValueError("Input file is not a raster dataset")
        
    band_indices = [int(b.strip()) for b in bands.split(",") if b.strip()]
    band_data = []
    
    for b in band_indices:
        band_data.append(reader.read_raster_band(b).astype(np.float32))
        
    # Stack bands into (height, width, band_count) and flatten
    stacked = np.stack(band_data, axis=-1)
    h, w, c = stacked.shape
    flattened = stacked.reshape(-1, c)
    
    # Handle NaN values
    flattened = np.nan_to_num(flattened, nan=0.0)
    
    # Fit KMeans clustering classification
    kmeans = KMeans(n_clusters=clusters, random_state=42, n_init='auto')
    labels = kmeans.fit_predict(flattened)
    classified = labels.reshape(h, w).astype(np.int16)
    
    with rasterio.open(input_path) as src:
        profile = src.profile.copy()
        profile.update(
            driver="GTiff",
            count=1,
            dtype=rasterio.int16
        )
        
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with rasterio.open(output_path, "w", **profile) as dst:
        dst.write(classified, 1)
        
    return output_path

# Register tools
registry.register(
    name="calculate_ndvi",
    label="Normalized Difference Vegetation Index (NDVI)",
    category="Raster Algebra",
    description="Calculates vegetation vigor (NDVI) from Red and NIR bands of a multi-spectral image.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-band remote sensing raster file"},
        {"name": "output_path", "label": "Output Raster File", "type": "file", "required": True, "help": "Path where the calculated NDVI GeoTIFF will be saved"},
        {"name": "red_band", "label": "Red Band Index", "type": "int", "default": 1, "required": True, "help": "Band index representing Red wavelength (1-indexed)"},
        {"name": "nir_band", "label": "NIR Band Index", "type": "int", "default": 2, "required": True, "help": "Band index representing Near-Infrared wavelength (1-indexed)"}
    ],
    fn=calculate_ndvi
)

registry.register(
    name="calculate_ndwi",
    label="Normalized Difference Water Index (NDWI)",
    category="Raster Algebra",
    description="Calculates open water body intensity (NDWI) from Green and NIR bands of a multi-spectral image.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-band remote sensing raster file"},
        {"name": "output_path", "label": "Output Raster File", "type": "file", "required": True, "help": "Path where the calculated NDWI GeoTIFF will be saved"},
        {"name": "green_band", "label": "Green Band Index", "type": "int", "default": 1, "required": True, "help": "Band index representing Green wavelength (1-indexed)"},
        {"name": "nir_band", "label": "NIR Band Index", "type": "int", "default": 2, "required": True, "help": "Band index representing Near-Infrared wavelength (1-indexed)"}
    ],
    fn=calculate_ndwi
)

registry.register(
    name="kmeans_classify",
    label="K-Means Unsupervised Classification",
    category="Classification",
    description="Classifies multi-spectral remote sensing pixels into arbitrary land cover groups using K-Means clustering.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-band remote sensing raster file"},
        {"name": "output_path", "label": "Output Classified Raster", "type": "file", "required": True, "help": "Path where the thematic classified GeoTIFF will be saved"},
        {"name": "bands", "label": "Spectral Bands (comma separated)", "type": "string", "default": "1,2,3", "required": True, "help": "Indices of bands to use in classification (e.g. 1,2,3)"},
        {"name": "clusters", "label": "Target Land cover classes", "type": "int", "default": 5, "required": True, "help": "Number of unique land cover clusters (K)"}
    ],
    fn=kmeans_classify
)

registry.register(
    name="calculate_dos1",
    label="DOS1 Atmospheric Correction",
    category="Preprocessing -> Atmospheric Correction",
    description="Performs Dark Object Subtraction (DOS1) to remove atmospheric haze from satellite imagery.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-spectral raster file needing atmospheric correction"},
        {"name": "output_path", "label": "Output Corrected Raster", "type": "file", "required": True, "help": "Path where the corrected GeoTIFF will be saved"}
    ],
    fn=calculate_dos1
)

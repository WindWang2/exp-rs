import os
import numpy as np
import rasterio
from sklearn.cluster import KMeans
from core.qgsreader import GeospatialReader
from analysis.qgsprocessingregistry import register_tool

@register_tool(
    name="kmeans_classify",
    label="K-Means Unsupervised Classification",
    category="Classification",
    description="Classifies multi-spectral remote sensing pixels into arbitrary land cover groups using K-Means clustering.",
    params=[
        {"name": "input_path", "label": "Input Raster File", "type": "file", "required": True, "help": "Path to the multi-band remote sensing raster file"},
        {"name": "output_path", "label": "Output Classified Raster", "type": "file", "required": True, "help": "Path where the thematic classified GeoTIFF will be saved"},
        {"name": "bands", "label": "Spectral Bands (comma separated)", "type": "string", "default": "1,2,3", "required": True, "help": "Indices of bands to use in classification (e.g. 1,2,3)"},
        {"name": "clusters", "label": "Target Land cover classes", "type": "int", "default": 5, "required": True, "help": "Number of unique land cover clusters (K)"}
    ]
)
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

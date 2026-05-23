import os
import pytest
import numpy as np
import rasterio
from rasterio.transform import from_origin
from engine._processing import calculate_ndvi, kmeans_classify

@pytest.fixture
def sample_spectral_raster(tmp_path):
    """Creates a mock multispectral 3-band raster where pixels have high NIR (veg) or low NIR (soil)."""
    file_path = str(tmp_path / "spectral_raster.tif")
    h, w = 16, 16
    transform = from_origin(0, 16, 1, 1)
    
    # Red band: low reflectance in crops (30), moderate in soil (80)
    red = np.full((h, w), 80, dtype=np.uint8)
    red[4:12, 4:12] = 30 # Center crop field
    
    # Green band
    green = np.full((h, w), 60, dtype=np.uint8)
    
    # NIR band: high reflectance in crops (220), moderate in soil (100)
    nir = np.full((h, w), 100, dtype=np.uint8)
    nir[4:12, 4:12] = 220
    
    profile = {
        'driver': 'GTiff',
        'dtype': 'uint8',
        'width': w,
        'height': h,
        'count': 3,
        'crs': 'EPSG:3857',
        'transform': transform
    }
    
    with rasterio.open(file_path, 'w', **profile) as dst:
        dst.write(red, 1)
        dst.write(green, 2)
        dst.write(nir, 3)
        
    return file_path

def test_calculate_ndvi(sample_spectral_raster, tmp_path):
    out_ndvi = str(tmp_path / "ndvi.tif")
    
    # Execute NDVI (Red = Band 1, NIR = Band 3)
    calculate_ndvi(sample_spectral_raster, out_ndvi, red_band=1, nir_band=3)
    
    assert os.path.exists(out_ndvi)
    
    with rasterio.open(out_ndvi) as src:
        assert src.count == 1
        assert src.dtypes[0] == rasterio.float32
        ndvi_data = src.read(1)
        
        # Crop center NDVI = (220 - 30) / (220 + 30) = 190 / 250 = +0.76
        # Background soil NDVI = (100 - 80) / (100 + 80) = 20 / 180 = +0.11
        assert ndvi_data[0, 0] == pytest.approx(0.1111, abs=1e-2)
        assert ndvi_data[8, 8] == pytest.approx(0.7600, abs=1e-2)

def test_kmeans_classify(sample_spectral_raster, tmp_path):
    out_class = str(tmp_path / "classified.tif")
    
    # Run KMeans with 2 target land classes on bands 1 and 3
    kmeans_classify(sample_spectral_raster, out_class, bands="1,3", clusters=2)
    
    assert os.path.exists(out_class)
    
    with rasterio.open(out_class) as src:
        assert src.count == 1
        assert src.dtypes[0] == rasterio.int16
        labels = src.read(1)
        
        # Center crop field pixels and background soil pixels should be clustered into different categories
        assert labels[0, 0] != labels[8, 8]

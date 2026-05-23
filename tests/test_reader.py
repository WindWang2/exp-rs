import os
import pytest
import numpy as np
import rasterio
from rasterio.transform import from_origin
from engine.core.reader import GeospatialReader

@pytest.fixture
def temp_raster(tmp_path):
    """Creates a small mock 3-band GeoTIFF for testing."""
    file_path = str(tmp_path / "test_raster.tif")
    h, w = 32, 32
    transform = from_origin(0, 32, 1, 1)
    
    # Fill bands with mock constants
    b1 = np.full((h, w), 50, dtype=np.uint8)
    b2 = np.full((h, w), 70, dtype=np.uint8)
    b3 = np.full((h, w), 120, dtype=np.uint8)
    
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
        dst.write(b1, 1)
        dst.write(b2, 2)
        dst.write(b3, 3)
        
    return file_path

def test_reader_raster_detection(temp_raster):
    reader = GeospatialReader(temp_raster)
    assert reader.is_raster is True
    
    meta = reader.metadata
    assert meta["type"] == "raster"
    assert meta["width"] == 32
    assert meta["height"] == 32
    assert meta["count"] == 3
    assert "3857" in meta["crs"]
    assert meta["bounds"]["left"] == 0.0
    assert meta["bounds"]["top"] == 32.0

def test_reader_read_raster_band(temp_raster):
    reader = GeospatialReader(temp_raster)
    
    # Read full resolution
    band1 = reader.read_raster_band(1)
    assert band1.shape == (32, 32)
    assert np.all(band1 == 50)
    
    band3 = reader.read_raster_band(3)
    assert np.all(band3 == 120)
    
    # Read downsampled overview
    downsampled = reader.read_raster_band(2, scale_factor=2)
    assert downsampled.shape == (16, 16)
    assert np.all(downsampled == 70)

def test_reader_band_index_error(temp_raster):
    reader = GeospatialReader(temp_raster)
    with pytest.raises(IndexError):
        reader.read_raster_band(4) # Out of range

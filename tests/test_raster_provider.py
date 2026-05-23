import pytest
import os
from engine.core.display.raster.provider import GDALDataProvider

def test_gdal_data_provider_extent():
    # Use existing sample data
    sample_path = "data/sample_crops.tif"
    if not os.path.exists(sample_path):
        pytest.skip("Sample data not found")
        
    provider = GDALDataProvider(sample_path)
    extent = provider.extent()
    
    assert "left" in extent
    assert "right" in extent
    assert "top" in extent
    assert "bottom" in extent
    assert extent["left"] < extent["right"]

import pytest
import os
from unittest.mock import MagicMock
from engine.core.display.raster.provider import GDALDataProvider
from engine.core.display.raster.layer import RasterLayer
from engine.core.display.base.map_settings import MapSettings

def test_raster_stack_integration():
    sample_path = "data/sample_crops.tif"
    if not os.path.exists(sample_path):
        pytest.skip("Sample data not found")
        
    # 1. Test Provider
    provider = GDALDataProvider(sample_path)
    extent = provider.extent()
    assert "left" in extent
    assert extent["left"] < extent["right"]
    
    # 2. Test Layer
    layer = RasterLayer("raster-1", "Sample Raster", sample_path)
    assert layer.id == "raster-1"
    assert layer.provider is not None
    assert layer.extent == extent
    
    # 3. Test Draw
    settings = MapSettings()
    settings.extent = extent
    
    painter = MagicMock()
    # Mocking drawImage to verify it's called
    layer.draw(painter, settings)
    
    assert painter.drawImage.called
    # First argument to drawImage(int, int, QImage) should be 0, 0
    args, kwargs = painter.drawImage.call_args
    assert args[0] == 0
    assert args[1] == 0

import numpy as np
import pytest
import rasterio
import os
from engine.preprocessing import calculate_dos1

def test_calculate_dos1_multiband(tmp_path):
    input_path = str(tmp_path / "input_multiband.tif")
    output_path = str(tmp_path / "output_multiband.tif")
    
    # Create a dummy 3-band 2x2 GeoTIFF
    # Band 1 min: 10 -> [0, 10, 20, 30]
    # Band 2 min: 50 -> [0, 10, 20, 30]
    # Band 3 min: 100 -> [0, 10, 20, 30]
    data = np.array([
        [[10, 20], [30, 40]],
        [[50, 60], [70, 80]],
        [[100, 110], [120, 130]]
    ], dtype=np.uint8)
    
    with rasterio.open(
        input_path, 'w',
        driver='GTiff',
        height=2,
        width=2,
        count=3,
        dtype=np.uint8,
        crs='EPSG:4326',
        transform=rasterio.transform.from_origin(0, 2, 1, 1)
    ) as dst:
        dst.write(data)
    
    # Run DOS1
    calculate_dos1(input_path, output_path)
    
    # Verify output
    with rasterio.open(output_path) as src:
        result_data = src.read()
        assert src.count == 3
        expected_band = np.array([[0, 10], [20, 30]], dtype=np.uint8)
        for i in range(3):
            assert np.array_equal(result_data[i], expected_band)

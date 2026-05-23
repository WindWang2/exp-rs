import numpy as np
import pytest
import rasterio
import os
from engine._preprocessing import calculate_dos1

def test_calculate_dos1_with_nodata(tmp_path):
    input_path = str(tmp_path / "input.tif")
    output_path = str(tmp_path / "output.tif")
    
    # Create a 1-band 4x4 GeoTIFF with nodata=0
    # Data: 0 is nodata, others are real values.
    # Min real value is 10.
    data = np.array([[
        [0, 10, 20, 30],
        [40, 0, 50, 60],
        [70, 80, 0, 90],
        [100, 110, 120, 0]
    ]], dtype=np.uint8)
    
    with rasterio.open(
        input_path, 'w',
        driver='GTiff',
        height=4,
        width=4,
        count=1,
        dtype=np.uint8,
        crs='EPSG:4326',
        transform=rasterio.transform.from_origin(0, 4, 1, 1),
        nodata=0
    ) as dst:
        dst.write(data)
    
    # Run DOS1
    calculate_dos1(input_path, output_path)
    
    # Verify output
    with rasterio.open(output_path) as src:
        result_data = src.read()
        # Dark value should be 10.
        # Expected: 0 stays 0. 10 becomes 0, 20 becomes 10, etc.
        expected_data = np.array([[
            [0, 0, 10, 20],
            [30, 0, 40, 50],
            [60, 70, 0, 80],
            [90, 100, 110, 0]
        ]], dtype=np.uint8)
        assert np.array_equal(result_data, expected_data)
        assert src.nodata == 0

def test_calculate_dos1_float(tmp_path):
    input_path = str(tmp_path / "input_float.tif")
    output_path = str(tmp_path / "output_float.tif")
    
    data = np.array([[
        [1.5, 2.5],
        [3.5, 4.5]
    ]], dtype=np.float32)
    
    with rasterio.open(
        input_path, 'w',
        driver='GTiff',
        height=2,
        width=2,
        count=1,
        dtype=np.float32,
        crs='EPSG:4326',
        transform=rasterio.transform.from_origin(0, 2, 1, 1)
    ) as dst:
        dst.write(data)
    
    calculate_dos1(input_path, output_path)
    
    with rasterio.open(output_path) as src:
        result_data = src.read()
        # Dark value is 1.5
        expected_data = np.array([[
            [0.0, 1.0],
            [2.0, 3.0]
        ]], dtype=np.float32)
        assert np.allclose(result_data, expected_data)

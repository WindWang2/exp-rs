import numpy as np
import pytest
import rasterio
import os
from engine.preprocessing import calculate_dos1_band, calculate_dos1

def test_calculate_dos1_band_basic():
    band = np.array([[10, 20], [30, 40]], dtype=np.uint8)
    expected = np.array([[0, 10], [20, 30]], dtype=np.uint8)
    result = calculate_dos1_band(band)
    assert np.array_equal(result, expected)

def test_calculate_dos1_band_uint16():
    band = np.array([[1000, 2000], [3000, 4000]], dtype=np.uint16)
    expected = np.array([[0, 1000], [2000, 3000]], dtype=np.uint16)
    result = calculate_dos1_band(band)
    assert result.dtype == np.uint16
    assert np.array_equal(result, expected)

def test_calculate_dos1_file_io(tmp_path):
    input_path = str(tmp_path / "input.tif")
    output_path = str(tmp_path / "output.tif")
    
    # Create a dummy 1-band 2x2 GeoTIFF
    data = np.array([[[10, 20], [30, 40]]], dtype=np.uint8)
    with rasterio.open(
        input_path, 'w',
        driver='GTiff',
        height=2,
        width=2,
        count=1,
        dtype=np.uint8,
        crs='EPSG:4326',
        transform=rasterio.transform.from_origin(0, 2, 1, 1)
    ) as dst:
        dst.write(data)
    
    # Run DOS1
    calculate_dos1(input_path, output_path)
    
    # Verify output
    assert os.path.exists(output_path)
    with rasterio.open(output_path) as src:
        result_data = src.read()
        expected_data = np.array([[[0, 10], [20, 30]]], dtype=np.uint8)
        assert np.array_equal(result_data, expected_data)
        assert src.profile['count'] == 1
        assert src.profile['dtype'] == 'uint8'

def test_calculate_dos1_with_nodata(tmp_path):
    input_path = str(tmp_path / "input_nodata.tif")
    output_path = str(tmp_path / "output_nodata.tif")
    
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

def test_rectify_coeffs():
    from engine.preprocessing.geometric.rectify import calculate_polynomial_coeffs
    # Simple shift: x -> x+10, y -> y+5
    src_pts = np.array([[0, 0], [1, 0], [0, 1]])
    dst_pts = np.array([[10, 5], [11, 5], [10, 6]])
    coeffs_x, coeffs_y = calculate_polynomial_coeffs(src_pts, dst_pts, order=1)
    # Verify coefficients match expected shift
    assert np.isclose(coeffs_x[1], 1.0) # x scale
    assert np.isclose(coeffs_x[0], 10.0) # x shift
    assert np.isclose(coeffs_y[2], 1.0) # y scale (y axis is 3rd column: 1, x, y)
    assert np.isclose(coeffs_y[0], 5.0) # y shift

def test_pca_pansharpen():
    from engine._preprocessing import pca_pansharpen_arrays
    import rasterio
    import os
    import sys
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../build'))

    # We mock it via in-memory arrays first
    # Or write a simple one that takes arrays instead of files for unit testing
    ms_bands = np.array([
        [[10, 20], [30, 40]],
        [[15, 25], [35, 45]],
        [[12, 22], [32, 42]]
    ], dtype=np.float32)

    pan_band = np.array([[50, 100], [150, 200]], dtype=np.float32)

    sharpened = pca_pansharpen_arrays(ms_bands, pan_band)

    assert sharpened.shape == ms_bands.shape
    assert sharpened.dtype == ms_bands.dtype

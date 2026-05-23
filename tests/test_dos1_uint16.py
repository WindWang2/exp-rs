import numpy as np
import pytest
from engine._preprocessing import calculate_dos1_band

def test_calculate_dos1_band_uint16():
    # Test with uint16 data
    band = np.array([[1000, 2000], [3000, 4000]], dtype=np.uint16)
    # dark_value = 1000
    # Expected: [[0, 1000], [2000, 3000]]
    expected = np.array([[0, 1000], [2000, 3000]], dtype=np.uint16)
    result = calculate_dos1_band(band)
    
    # This is expected to FAIL if it's hardcoded to uint8/255
    assert result.dtype == np.uint16
    assert np.array_equal(result, expected)

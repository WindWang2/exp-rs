import os
import pytest
import numpy as np
from PySide6.QtGui import QImage, QPainter
from PySide6.QtCore import QSize, QRectF, QPointF
from PySide6.QtWidgets import QApplication
import sys

# Ensure build directory is in sys.path
build_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build"))
if build_path not in sys.path:
    sys.path.insert(0, build_path)

import raster_ops

@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])

def test_cpp_warp_raster_band_identity():
    # 2x2 input
    input_band = np.array([[10.0, 20.0], [30.0, 40.0]], dtype=np.float32)
    # Identity coefficients mapping (u, v) -> (col, row) where col = u, row = v
    coeffs_x = np.array([0.0, 1.0, 0.0], dtype=np.float64)
    coeffs_y = np.array([0.0, 0.0, 1.0], dtype=np.float64)
    
    out = raster_ops.warp_raster_band(input_band, 2, 2, coeffs_x, coeffs_y)
    
    # Bilinear identity check
    assert np.allclose(out, input_band), f"C++ warp identity failed: {out}"

def test_cpp_warp_raster_band_scale_translation():
    # 2x2 input
    input_band = np.array([[100.0, 200.0], [300.0, 400.0]], dtype=np.float32)
    
    # Translate and scale: shift by 0.5 and scale by 0.5
    coeffs_x = np.array([0.5, 0.5, 0.0], dtype=np.float64)
    coeffs_y = np.array([0.5, 0.0, 0.5], dtype=np.float64)
    
    out = raster_ops.warp_raster_band(input_band, 2, 2, coeffs_x, coeffs_y)
    
    # Check that bilinear interpolation matches expected values
    # At (0,0): src_x = 0.5, src_y = 0.5
    # Interpolated value of [[100, 200], [300, 400]] at (0.5, 0.5) is 250.0
    assert np.allclose(out[0, 0], 250.0), f"Expected 250.0, got {out[0, 0]}"

def test_raster_layer_renderer_warp(app):
    from core.qgsproject import GISProject
    from core.raster.qgsrasterlayer import RasterLayer
    from core.qgsmapsettings import MapSettings
    
    raster_path = os.path.join(os.path.dirname(__file__), "../data", "sample_crops.tif")
    if not os.path.exists(raster_path):
        pytest.skip("Sample crops data not found")
        
    layer = RasterLayer("l1", "Crops", raster_path)
    
    settings = MapSettings()
    settings.layers = [layer]
    settings.extent = layer.raw_extent
    settings.output_size = QSize(256, 256)
    settings.destination_crs = layer.crs
    
    renderer = layer.createMapRenderer(settings)
    assert renderer is not None
    
    # Render onto QImage
    img = QImage(256, 256, QImage.Format_RGB888)
    img.fill(0)
    
    painter = QPainter(img)
    try:
        renderer.render(painter, settings)
    finally:
        painter.end()
        
    # Check that image is correctly shaped and rendered
    ptr = img.bits()
    arr = np.frombuffer(ptr, dtype=np.uint8).reshape((256, 256, 3))
    assert arr.shape == (256, 256, 3)


def test_cpp_warp_and_compose_rgb():
    # 2x2 input bands
    r = np.array([[10.0, 20.0], [30.0, 40.0]], dtype=np.float32)
    g = np.array([[50.0, 60.0], [70.0, 80.0]], dtype=np.float32)
    b = np.array([[90.0, 100.0], [110.0, 120.0]], dtype=np.float32)
    
    coeffs_x = np.array([0.0, 1.0, 0.0], dtype=np.float64)
    coeffs_y = np.array([0.0, 0.0, 1.0], dtype=np.float64)
    
    out = raster_ops.warp_and_compose_rgb(
        r, g, b, 2, 2, coeffs_x, coeffs_y,
        10.0, 40.0,
        50.0, 80.0,
        90.0, 120.0
    )
    
    assert out.shape == (2, 2, 3)
    # Since bounds are min/max of each band, they should stretch to 0 and 255 exactly!
    assert np.allclose(out[0, 0], [0, 0, 0])
    assert np.allclose(out[1, 1], [255, 255, 255])


def test_cpp_warp_and_stretch_gray():
    band = np.array([[10.0, 20.0], [30.0, 40.0]], dtype=np.float32)
    coeffs_x = np.array([0.0, 1.0, 0.0], dtype=np.float64)
    coeffs_y = np.array([0.0, 0.0, 1.0], dtype=np.float64)
    
    out = raster_ops.warp_and_stretch_gray(
        band, 2, 2, coeffs_x, coeffs_y,
        10.0, 40.0
    )
    
    assert out.shape == (2, 2, 3)
    assert np.allclose(out[0, 0], [0, 0, 0])
    assert np.allclose(out[1, 1], [255, 255, 255])


import pytest
import os
from engine.core.display.vector.provider import OGRDataProvider
from engine.core.display.vector.layer import VectorLayer
from engine.core.display.renderers.vector.single_symbol import SingleSymbolRenderer
from engine.core.display.base.map_settings import MapSettings
from PySide6.QtGui import QPainter, QImage
from PySide6.QtCore import QSize, QRectF

def test_vector_layer_initialization():
    path = "data/test_vectors.geojson"
    layer = VectorLayer("v1", "Test Vector", path)

    assert layer.id == "v1"
    # Expected extent from provider: left=5, bottom=5, right=20, top=20
    # QRectF(left, top, width, height) -> QRectF(5, 20, 15, 15)
    expected_extent = QRectF(5, 20, 15, 15)
    assert layer.raw_extent == expected_extent

def test_vector_layer_draw():

    path = "data/test_vectors.geojson"
    provider = OGRDataProvider(path)
    
    extent = provider.extent()
    assert extent['left'] == 5.0
    assert extent['bottom'] == 5.0
    assert extent['right'] == 20.0
    assert extent['top'] == 20.0
    
    features = provider.get_features()
    assert len(features) == 3
    
    # Test spatial filter
    # Only Point A and Polygon A should be in this extent
    small_extent = {'left': 0, 'bottom': 0, 'right': 12, 'top': 12}
    filtered = provider.get_features(small_extent)
    assert len(filtered) == 2
    ids = [f['properties']['id'] for f in filtered]
    assert 1 in ids
    assert 3 in ids
    assert 2 not in ids

def test_vector_layer_draw():
    path = "data/test_vectors.geojson"
    layer = VectorLayer("v1", "Test Vector", path)
    
    settings = MapSettings()
    settings.extent = layer.extent
    settings.output_size = QSize(400, 400)
    
    # Create a painter on an image
    img = QImage(400, 400, QImage.Format_ARGB32)
    img.fill(0)
    painter = QPainter(img)
    
    try:
        layer.draw(painter, settings)
    finally:
        painter.end()
    
    # We can't easily verify the pixels without complex math, 
    # but we can ensure it didn't crash and did *something*
    # (Checking if any pixels are non-zero)
    non_zero = False
    for y in range(img.height()):
        for x in range(img.width()):
            if img.pixelColor(x, y).alpha() != 0:
                non_zero = True
                break
        if non_zero: break
    
    # Actually, coordinates 10,10 etc are very small in a 400x400 image
    # and might be drawn if they aren't transformed.
    # Our simple renderer just uses raw coordinates.
    assert non_zero

def test_renderer_assignment():
    path = "data/test_vectors.geojson"
    layer = VectorLayer("v1", "Test Vector", path)
    
    old_renderer = layer.renderer
    new_renderer = SingleSymbolRenderer()
    layer.set_renderer(new_renderer)
    
    assert layer.renderer == new_renderer
    assert layer.renderer != old_renderer

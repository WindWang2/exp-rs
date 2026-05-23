import os
import pytest
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QImage, QPainter, QColor
from PySide6.QtCore import QRectF, QSize
from engine.core.display.raster.layer import RasterLayer
from engine.core.display.vector.layer import VectorLayer
from engine.core.display.base.map_settings import MapSettings
from gui.properties_dialog import LayerPropertiesDialog

@pytest.fixture(scope="module")
def app():
    """Ensure a single QApplication instance exists for GUI tests."""
    return QApplication.instance() or QApplication([])

def test_raster_layer_symbology_and_reprojection(app):
    raster_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "sample_crops.tif")
    assert os.path.exists(raster_path)
    
    # Instantiate RasterLayer
    layer = RasterLayer("test_raster", "Crops Sample", raster_path)
    
    # Assert defaults
    assert layer.render_type in ["multiband", "grayscale"]
    assert layer.opacity == 1.0
    assert layer.min_val is None
    assert layer.max_val is None
    
    # Modify advanced properties
    layer.render_type = "pseudocolor"
    layer.pseudocolor_band = 1
    layer.color_ramp = "viridis"
    layer.min_val = 10.0
    layer.max_val = 250.0
    layer.opacity = 0.75
    
    # Set up mock canvas settings & painter to simulate draw() call
    settings = MapSettings()
    settings.layers = [layer]
    settings.extent = layer.extent
    settings.output_size = QSize(100, 100)
    
    img = QImage(QSize(100, 100), QImage.Format_ARGB32)
    painter = QPainter(img)
    try:
        # Should execute successfully without throwing exceptions
        layer.draw(painter, settings)
    finally:
        painter.end()
        
    # Check projection: sample_crops.tif might be EPSG:32650 (UTM).
    # If self.crs is different from EPSG:3857, self.extent should not equal self.raw_extent.
    if layer.crs and layer.crs != "EPSG:3857":
        assert layer.extent != layer.raw_extent
    else:
        assert layer.extent == layer.raw_extent

def test_vector_layer_symbology_and_reprojection(app):
    vector_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "test_vectors.geojson")
    assert os.path.exists(vector_path)
    
    # Instantiate VectorLayer
    layer = VectorLayer("test_vector", "Test Vector", vector_path)
    
    # Assert defaults and setters of SingleSymbolRenderer
    renderer = layer.renderer
    assert renderer.stroke_width() == 1
    
    renderer.set_stroke_width(3)
    renderer.set_color(QColor(0, 255, 0, 150))
    renderer.set_stroke_color(QColor(255, 255, 255))
    
    assert renderer.stroke_width() == 3
    assert renderer.color() == QColor(0, 255, 0, 150)
    assert renderer.stroke_color() == QColor(255, 255, 255)
    
    # Set up mock settings & painter
    settings = MapSettings()
    settings.layers = [layer]
    settings.extent = layer.extent
    settings.output_size = QSize(100, 100)
    
    img = QImage(QSize(100, 100), QImage.Format_ARGB32)
    painter = QPainter(img)
    try:
        # Should draw successfully
        layer.draw(painter, settings)
    finally:
        painter.end()

def test_layer_properties_dialog_instantiation(app, qtbot):
    raster_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "sample_crops.tif")
    layer = RasterLayer("test_raster", "Crops Sample", raster_path)
    
    dialog = LayerPropertiesDialog(layer)
    qtbot.addWidget(dialog)
    
    # Verify properties tab elements
    assert dialog.name_edit.text() == "Crops Sample"
    assert dialog.render_type_combo.count() == 3
    assert dialog.opacity_slider.value() == 100
    
    # Test changing combobox
    dialog.render_type_combo.setCurrentIndex(2) # Pseudocolor
    assert dialog.pseudo_widget.isHidden() is False
    assert dialog.rgb_widget.isHidden() is True

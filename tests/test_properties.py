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
    
    # Assert QGIS-aligned defaults
    assert layer.render_type in ["multiband", "grayscale"]
    assert layer.contrast_enhancement == "stretch_to_min_max"
    assert layer.min_max_limits_method == "cumulative_cut"
    assert layer.cumulative_cut_lower == 2.0
    assert layer.cumulative_cut_upper == 98.0
    
    # 1. Test drawing with Cumulative Cut (2% - 98%)
    settings = MapSettings()
    settings.layers = [layer]
    settings.extent = layer.extent
    settings.output_size = QSize(100, 100)
    
    img = QImage(QSize(100, 100), QImage.Format_ARGB32)
    painter = QPainter(img)
    try:
        layer.draw(painter, settings)
    finally:
        painter.end()

    # 2. Test drawing with Std Dev Stretch (Mean +/- 2*std)
    layer.min_max_limits_method = "std_dev"
    layer.std_dev_factor = 2.0
    layer.opacity = 0.8
    
    painter = QPainter(img)
    try:
        layer.draw(painter, settings)
    finally:
        painter.end()

    # 3. Test drawing with Pseudocolor & User Defined boundaries
    layer.render_type = "pseudocolor"
    layer.color_ramp = "jet"
    layer.min_max_limits_method = "user_defined"
    layer.user_min = 20.0
    layer.user_max = 200.0
    
    painter = QPainter(img)
    try:
        layer.draw(painter, settings)
    finally:
        painter.end()
        
    # Check projection: sample_crops.tif might be EPSG:32650 (UTM).
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
    
    # Check that Min/Max settings match
    assert dialog.contrast_combo.currentIndex() == 1  # Stretch to MinMax is default
    assert dialog.radio_cumulative.isChecked() is True
    
    # Test changing render type combobox to Pseudocolor
    dialog.render_type_combo.setCurrentIndex(2)
    assert dialog.pseudo_widget.isHidden() is False
    assert dialog.rgb_widget.isHidden() is True

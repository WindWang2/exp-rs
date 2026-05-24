import sys
from PySide6.QtWidgets import QApplication
_app = QApplication.instance() or QApplication(sys.argv)

from core.scalebar.qgsscalebarsettings import QgsScaleBarSettings

def test_scalebar_settings_defaults():
    settings = QgsScaleBarSettings()
    assert settings.numberOfSegments() == 2
    assert settings.unitsPerSegment() == 1.0

def test_scalebar_settings_setters():
    settings = QgsScaleBarSettings()
    settings.setNumberOfSegments(4)
    settings.setUnitsPerSegment(1000)
    settings.setUnitLabel("km")
    assert settings.numberOfSegments() == 4
    assert settings.unitsPerSegment() == 1000
    assert settings.unitLabel() == "km"

def test_scalebar_settings_height():
    settings = QgsScaleBarSettings()
    settings.setHeight(8)
    assert settings.height() == 8

from core.scalebar.qgsscalebarrenderer import QgsScaleBarRenderer

def test_scalebar_renderer_create():
    settings = QgsScaleBarSettings()
    renderer = QgsScaleBarRenderer(settings)
    assert renderer.settings() is settings

def test_scalebar_renderer_calculate_width():
    """Renderer should calculate pixel width based on map scale."""
    settings = QgsScaleBarSettings()
    settings.setNumberOfSegments(2)
    settings.setUnitsPerSegment(1000)  # 1000 meters per segment
    renderer = QgsScaleBarRenderer(settings)
    # At scale 1:1000000, 1000m = 1mm on ground
    # At 96 DPI, 1mm = 96/25.4 ≈ 3.78 pixels
    width = renderer.calculateWidth(1000000)
    assert width > 0

def test_scalebar_renderer_render():
    """Renderer should not crash when painting."""
    from PySide6.QtGui import QImage, QPainter
    from PySide6.QtCore import QSize, QPointF
    settings = QgsScaleBarSettings()
    settings.setNumberOfSegments(2)
    settings.setUnitsPerSegment(1000)
    settings.setUnitLabel("km")
    renderer = QgsScaleBarRenderer(settings)
    img = QImage(QSize(400, 100), QImage.Format_ARGB32)
    img.fill(0xFFFFFFFF)
    painter = QPainter(img)
    renderer.render(painter, 1000000, QPointF(10, 80))
    painter.end()

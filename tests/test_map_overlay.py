# tests/test_map_overlay.py
import sys
from PySide6.QtWidgets import QApplication

_app = QApplication.instance() or QApplication(sys.argv)

from gui.map_overlay import MapOverlayContainer
from gui.canvas import MapCanvas


def test_overlay_wraps_canvas_and_resizes():
    c = MapOverlayContainer()
    assert isinstance(c.canvas, MapCanvas)
    c.resize(800, 600)
    c.show()
    _app.processEvents()
    # band combo + scale bar overlays exist and stay within bounds
    assert c.scale_bar.parent() is c
    assert c.band_combo.parent() is c
    assert c.band_combo.x() + c.band_combo.width() <= c.width() + 1
    c.set_image_info("Sentinel-2A · L2A · 2025-04-12 · 4-3-2")
    assert "Sentinel" in c.info_pill.text()

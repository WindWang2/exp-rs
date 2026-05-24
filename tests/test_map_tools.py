import sys
from PySide6.QtWidgets import QApplication
_app = QApplication.instance() or QApplication(sys.argv)

from gui.qgsmaptool import QgsMapTool

def test_map_tool_has_canvas():
    from gui.qgsmapcanvas import QgsMapCanvas
    canvas = QgsMapCanvas()
    tool = QgsMapTool(canvas)
    assert tool.canvas() is canvas

def test_map_tool_to_map_coordinates():
    """toMapCoordinates should convert pixel to world."""
    from gui.qgsmapcanvas import QgsMapCanvas
    from core.qgsrectangle import QgsRectangle
    from PySide6.QtCore import QPoint
    canvas = QgsMapCanvas()
    canvas.resize(500, 500)
    canvas.setExtent(QgsRectangle(0, 0, 100, 100))
    tool = QgsMapTool(canvas)
    pt = tool.toMapCoordinates(QPoint(250, 250))
    assert pt is not None

def test_map_tool_activate_deactivate():
    from gui.qgsmapcanvas import QgsMapCanvas
    canvas = QgsMapCanvas()
    tool = QgsMapTool(canvas)
    tool.activate()
    tool.deactivate()

def test_map_tool_is_not_zoom_tool():
    from gui.qgsmapcanvas import QgsMapCanvas
    canvas = QgsMapCanvas()
    tool = QgsMapTool(canvas)
    assert tool.isZoomTool() == False

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

# --- QgsMapToolIdentify tests ---

def test_identify_tool_is_map_tool():
    from gui.qgsmaptoolidentify import QgsMapToolIdentify
    from gui.qgsmaptool import QgsMapTool
    from gui.qgsmapcanvas import QgsMapCanvas
    canvas = QgsMapCanvas()
    tool = QgsMapToolIdentify(canvas)
    assert isinstance(tool, QgsMapTool)

def test_identify_tool_emits_signal():
    from gui.qgsmaptoolidentify import QgsMapToolIdentify
    from gui.qgsmapcanvas import QgsMapCanvas
    from core.qgsrectangle import QgsRectangle
    from PySide6.QtGui import QMouseEvent
    from PySide6.QtCore import QPointF, Qt
    canvas = QgsMapCanvas()
    canvas.setExtent(QgsRectangle(0, 0, 100, 100))
    canvas.resize(500, 500)
    tool = QgsMapToolIdentify(canvas)
    received = []
    tool.identifySignal.connect(lambda pt: received.append(pt))
    tool.activate()
    event = QMouseEvent(QMouseEvent.Type.MouseButtonPress, QPointF(250, 250),
                        QPointF(250, 250), Qt.LeftButton, Qt.LeftButton, Qt.NoModifier)
    tool.canvasPressEvent(event)
    assert len(received) == 1


# --- QgsMapToolZoom tests ---

def test_zoom_tool_is_map_tool():
    from gui.qgsmaptoolzoom import QgsMapToolZoom
    from gui.qgsmapcanvas import QgsMapCanvas
    canvas = QgsMapCanvas()
    tool = QgsMapToolZoom(canvas)
    assert isinstance(tool, QgsMapTool)

def test_zoom_tool_is_zoom_tool():
    from gui.qgsmaptoolzoom import QgsMapToolZoom
    from gui.qgsmapcanvas import QgsMapCanvas
    canvas = QgsMapCanvas()
    tool = QgsMapToolZoom(canvas)
    assert tool.isZoomTool()

def test_zoom_tool_cursor():
    from gui.qgsmaptoolzoom import QgsMapToolZoom
    from gui.qgsmapcanvas import QgsMapCanvas
    from PySide6.QtCore import Qt
    canvas = QgsMapCanvas()
    tool = QgsMapToolZoom(canvas)
    tool.activate()
    assert canvas.cursor().shape() == Qt.CrossCursor


# --- QgsMapToolPan tests ---

def test_pan_tool_is_map_tool():
    from gui.qgsmaptoolpan import QgsMapToolPan
    from gui.qgsmaptool import QgsMapTool
    from gui.qgsmapcanvas import QgsMapCanvas
    canvas = QgsMapCanvas()
    tool = QgsMapToolPan(canvas)
    assert isinstance(tool, QgsMapTool)

def test_pan_tool_cursor():
    from gui.qgsmaptoolpan import QgsMapToolPan
    from gui.qgsmapcanvas import QgsMapCanvas
    from PySide6.QtCore import Qt
    canvas = QgsMapCanvas()
    tool = QgsMapToolPan(canvas)
    tool.activate()
    assert canvas.cursor().shape() == Qt.OpenHandCursor

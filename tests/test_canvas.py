import sys
from PySide6.QtWidgets import QApplication

# Ensure QApplication exists before any widget operations
_app = QApplication.instance() or QApplication(sys.argv)

from gui.qgsmapcanvasmap import QgsMapCanvasMap


def test_canvas_map_pixmap_item():
    """QgsMapCanvasMap should be a QGraphicsPixmapItem."""
    from PySide6.QtWidgets import QGraphicsScene
    scene = QGraphicsScene()
    cmap = QgsMapCanvasMap(scene)
    assert cmap in scene.items()


def test_canvas_map_set_image():
    """Setting an image should update the pixmap."""
    from PySide6.QtGui import QImage, QPixmap
    from PySide6.QtCore import QSize
    from PySide6.QtWidgets import QGraphicsScene
    scene = QGraphicsScene()
    cmap = QgsMapCanvasMap(scene)
    img = QImage(QSize(100, 100), QImage.Format_ARGB32)
    cmap.setImage(img)
    assert not cmap.pixmap().isNull()


def test_canvas_map_clear():
    """Clearing should set a null pixmap."""
    from PySide6.QtGui import QImage
    from PySide6.QtCore import QSize
    from PySide6.QtWidgets import QGraphicsScene
    scene = QGraphicsScene()
    cmap = QgsMapCanvasMap(scene)
    cmap.setImage(QImage(QSize(100, 100), QImage.Format_ARGB32))
    cmap.clear()
    assert cmap.pixmap().isNull()


# --- QgsMapCanvas (QGraphicsView rewrite) tests ---

from gui.qgsmapcanvas import QgsMapCanvas


def test_canvas_is_qgraphics_view():
    from PySide6.QtWidgets import QGraphicsView
    canvas = QgsMapCanvas()
    assert isinstance(canvas, QGraphicsView)


def test_canvas_has_scene():
    canvas = QgsMapCanvas()
    assert canvas.scene() is not None


def test_canvas_has_map_item():
    canvas = QgsMapCanvas()
    assert canvas._map_item is not None


def test_canvas_set_extent():
    from core.qgsrectangle import QgsRectangle
    canvas = QgsMapCanvas()
    canvas.setExtent(QgsRectangle(0, 0, 100, 100))
    ext = canvas.extent()
    assert ext is not None
    assert ext.width() == 100


def test_canvas_set_layers():
    canvas = QgsMapCanvas()
    canvas.setLayers([])
    assert canvas.layers() == []


def test_canvas_map_to_pixel():
    from core.qgsrectangle import QgsRectangle
    canvas = QgsMapCanvas()
    canvas.resize(500, 500)
    canvas.setExtent(QgsRectangle(0, 0, 100, 100))
    mtp = canvas.mapToPixel()
    assert mtp is not None


def test_canvas_refresh():
    """refresh() should not crash with no layers."""
    from core.qgsrectangle import QgsRectangle
    canvas = QgsMapCanvas()
    canvas.setExtent(QgsRectangle(0, 0, 100, 100))
    canvas.refresh()


# --- Integration tests: signals, map tools, preview ---

def test_canvas_signal_extents_changed():
    """Setting extent should emit extentsChanged."""
    from core.qgsrectangle import QgsRectangle
    canvas = QgsMapCanvas()
    received = []
    canvas.extentsChanged.connect(lambda: received.append(True))
    canvas.setExtent(QgsRectangle(0, 0, 100, 100))
    assert len(received) == 1


def test_canvas_signal_layers_changed():
    """Setting layers should emit layersChanged."""
    canvas = QgsMapCanvas()
    received = []
    canvas.layersChanged.connect(lambda: received.append(True))
    canvas.setLayers([])
    assert len(received) == 1


def test_canvas_map_tool_events():
    """Map tool should receive events through canvas."""
    from gui.qgsmaptool import QgsMapTool
    from core.qgsrectangle import QgsRectangle
    from PySide6.QtGui import QMouseEvent
    from PySide6.QtCore import QPointF, Qt
    canvas = QgsMapCanvas()
    canvas.setExtent(QgsRectangle(0, 0, 100, 100))
    canvas.resize(500, 500)
    events = []

    class TestTool(QgsMapTool):
        def canvasPressEvent(self, event):
            events.append('press')

    tool = TestTool(canvas)
    canvas.setMapTool(tool)
    event = QMouseEvent(QMouseEvent.Type.MouseButtonPress, QPointF(100, 100),
                        QPointF(100, 100), Qt.LeftButton, Qt.LeftButton, Qt.NoModifier)
    canvas.mousePressEvent(event)
    assert 'press' in events


def test_canvas_set_map_tool():
    """setMapTool should activate new tool and deactivate old."""
    from gui.qgsmaptool import QgsMapTool
    from core.qgsrectangle import QgsRectangle
    canvas = QgsMapCanvas()
    activations = []

    class TrackTool(QgsMapTool):
        def activate(self): activations.append('activate')
        def deactivate(self): activations.append('deactivate')

    tool1 = TrackTool(canvas)
    tool2 = TrackTool(canvas)
    canvas.setMapTool(tool1)
    assert activations == ['activate']
    canvas.setMapTool(tool2)
    assert activations == ['activate', 'deactivate', 'activate']


def test_canvas_render_preview():
    """renderPreview should not crash."""
    from core.qgsrectangle import QgsRectangle
    canvas = QgsMapCanvas()
    canvas.setExtent(QgsRectangle(0, 0, 100, 100))
    canvas.resize(500, 500)
    canvas.renderPreview()

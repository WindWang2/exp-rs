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

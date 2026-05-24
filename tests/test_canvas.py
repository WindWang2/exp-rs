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

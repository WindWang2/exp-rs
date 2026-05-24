from PySide6.QtWidgets import QGraphicsScene, QGraphicsPixmapItem
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtCore import QPointF


class QgsMapCanvasMap(QGraphicsPixmapItem):
    """
    A QGraphicsPixmapItem that holds the rendered map image in a QGraphicsScene.

    Mirrors the role of QgsMapCanvasMap in QGIS C++:
    - Constructed with a scene reference and adds itself to the scene
    - setImage() converts a QImage to a QPixmap and displays it
    - clear() resets the pixmap to null
    """

    def __init__(self, scene: QGraphicsScene, parent=None):
        super().__init__(parent)
        self._scene = scene
        self._scene.addItem(self)

    def setImage(self, image: QImage):
        """Convert the given QImage to a QPixmap and set it as this item's pixmap."""
        self.setPixmap(QPixmap.fromImage(image))

    def clear(self):
        """Set a null pixmap, effectively clearing the rendered image."""
        self.setPixmap(QPixmap())

    def setPos(self, x: float, y: float):
        """Position this item in the scene. Delegates to QGraphicsItem.setPos."""
        super().setPos(x, y)


MapCanvasMap = QgsMapCanvasMap

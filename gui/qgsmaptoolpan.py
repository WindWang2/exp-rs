from PySide6.QtCore import Qt
from PySide6.QtGui import QCursor
from PySide6.QtWidgets import QGraphicsView

from gui.qgsmaptool import QgsMapTool


class QgsMapToolPan(QgsMapTool):
    """Pan tool using QGraphicsView's built-in scroll hand drag."""

    def __init__(self, canvas):
        super().__init__(canvas)

    def activate(self):
        self._canvas.setDragMode(QGraphicsView.ScrollHandDrag)
        self._canvas.setCursor(QCursor(Qt.OpenHandCursor))

    def deactivate(self):
        self._canvas.setDragMode(QGraphicsView.NoDrag)
        self._canvas.setCursor(QCursor(Qt.ArrowCursor))

    def canvasPressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self._canvas.setCursor(QCursor(Qt.ClosedHandCursor))

    def canvasReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            self._canvas.setCursor(QCursor(Qt.OpenHandCursor))
            # After drag, sync the canvas extent with the new viewport
            self._sync_extent_from_viewport()

    def _sync_extent_from_viewport(self):
        """Update canvas extent to match what QGraphicsView scrolled to."""
        view = self._canvas
        center = view.mapToScene(view.viewport().rect().center())
        from core.qgsrectangle import QgsRectangle
        ext = view.extent()
        half_w = ext.width() / 2.0
        half_h = ext.height() / 2.0
        new_ext = QgsRectangle(
            center.x() - half_w, center.y() - half_h,
            center.x() + half_w, center.y() + half_h
        )
        view.setExtent(new_ext)
        view.refresh()


MapToolPan = QgsMapToolPan

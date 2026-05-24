from PySide6.QtCore import Qt, QPoint, QRect
from PySide6.QtGui import QCursor
from PySide6.QtWidgets import QRubberBand

from gui.qgsmaptool import QgsMapTool
from core.qgsrectangle import QgsRectangle


class QgsMapToolZoom(QgsMapTool):
    """Rubber band zoom tool. Drag to zoom in, or click to zoom out."""

    def __init__(self, canvas, zoom_in=True):
        super().__init__(canvas)
        self._zoom_in = zoom_in
        self._rubber_band = None
        self._start_pos = None

    def activate(self):
        self._canvas.setCursor(QCursor(Qt.CrossCursor))

    def deactivate(self):
        self._cleanup_rubber_band()
        self._canvas.setCursor(QCursor(Qt.ArrowCursor))

    def isZoomTool(self):
        return True

    def canvasPressEvent(self, event):
        if event.button() == Qt.LeftButton:
            if self._zoom_in:
                self._start_pos = event.pos()
                self._rubber_band = QRubberBand(QRubberBand.Rectangle, self._canvas.viewport())
                self._rubber_band.setGeometry(QRect(self._start_pos, self._start_pos))
                self._rubber_band.show()
            else:
                # Zoom out: click centers, zooms out by 2x
                center = self.toMapCoordinates(event.pos())
                ext = self._canvas.extent()
                new_w = ext.width() * 2.0
                new_h = ext.height() * 2.0
                self._canvas.setExtent(QgsRectangle(
                    center.x() - new_w / 2, center.y() - new_h / 2,
                    center.x() + new_w / 2, center.y() + new_h / 2
                ))
                self._canvas.refresh()

    def canvasMoveEvent(self, event):
        if self._rubber_band and self._start_pos:
            self._rubber_band.setGeometry(QRect(self._start_pos, event.pos()).normalized())

    def canvasReleaseEvent(self, event):
        if event.button() == Qt.LeftButton and self._rubber_band and self._start_pos:
            end_pos = event.pos()
            rect = QRect(self._start_pos, end_pos).normalized()

            # Convert viewport rect to map coordinates
            top_left = self.toMapCoordinates(rect.topLeft())
            bottom_right = self.toMapCoordinates(rect.bottomRight())

            self._cleanup_rubber_band()

            # Only zoom if the rect is meaningful (> 5 pixels)
            if rect.width() > 5 and rect.height() > 5:
                self._canvas.setExtent(QgsRectangle(
                    top_left.x(), bottom_right.y(),
                    bottom_right.x(), top_left.y()
                ))
                self._canvas.refresh()

    def _cleanup_rubber_band(self):
        if self._rubber_band:
            self._rubber_band.hide()
            self._rubber_band.deleteLater()
            self._rubber_band = None
        self._start_pos = None


MapToolZoom = QgsMapToolZoom

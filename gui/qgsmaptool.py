from PySide6.QtCore import Qt, QPointF
from PySide6.QtGui import QCursor

from core.qgspointxy import QgsPointXY


class QgsMapTool:
    """Base class for map tools. Subclasses override canvasPressEvent/ReleaseEvent/MoveEvent."""

    def __init__(self, canvas):
        self._canvas = canvas

    def canvas(self):
        return self._canvas

    def toMapCoordinates(self, point):
        """Convert viewport pixel QPoint to map coordinates QgsPointXY."""
        settings = self._canvas.mapSettings()
        world_pos = settings.deviceToWorld().map(QPointF(point))
        return QgsPointXY(world_pos.x(), world_pos.y())

    def activate(self):
        """Called when tool becomes active."""
        pass

    def deactivate(self):
        """Called when tool becomes inactive."""
        pass

    def canvasPressEvent(self, event):
        """Override in subclasses. event is a QMouseEvent."""
        pass

    def canvasReleaseEvent(self, event):
        """Override in subclasses."""
        pass

    def canvasMoveEvent(self, event):
        """Override in subclasses."""
        pass

    def canvasDoubleClickEvent(self, event):
        """Override in subclasses."""
        pass

    # --- Backward compatibility: old API delegates to new API ---
    def mousePressEvent(self, event):
        """Backward compat: delegates to canvasPressEvent."""
        self.canvasPressEvent(event)

    def mouseReleaseEvent(self, event):
        """Backward compat: delegates to canvasReleaseEvent."""
        self.canvasReleaseEvent(event)

    def mouseMoveEvent(self, event):
        """Backward compat: delegates to canvasMoveEvent."""
        self.canvasMoveEvent(event)

    def wheelEvent(self, event):
        """Default wheel zoom around cursor position."""
        canvas = self._canvas
        if canvas.extent().isEmpty():
            return

        # Map pixel to world
        world_pos = self.toMapCoordinates(event.position().toPoint())

        # Zoom factor
        angle = event.angleDelta().y()
        factor = 1.0 / canvas._zoom_factor if angle > 0 else canvas._zoom_factor

        # Scale extent around cursor
        ext = canvas.extent()
        new_width = ext.width() * factor
        new_height = ext.height() * factor

        rel_x = (world_pos.x() - ext.xMinimum()) / ext.width()
        rel_y = (world_pos.y() - ext.yMinimum()) / ext.height()

        new_left = world_pos.x() - rel_x * new_width
        new_bottom = world_pos.y() - rel_y * new_height

        from core.qgsrectangle import QgsRectangle
        canvas.setExtent(QgsRectangle(new_left, new_bottom, new_left + new_width, new_bottom + new_height))
        canvas.refresh()

    def isZoomTool(self):
        return False

    def cursor(self):
        return QCursor(Qt.ArrowCursor)


MapTool = QgsMapTool

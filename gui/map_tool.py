from PySide6.QtCore import Qt, QPointF, QRectF
from PySide6.QtGui import QCursor
import enum

class MapTool:
    """
    Abstract base class for map tools (e.g., pan, zoom, selection).
    Equivalent to QgsMapTool in QGIS.
    """
    def __init__(self, canvas):
        self._canvas = canvas

    def canvas(self):
        return self._canvas

    def activate(self):
        """Called when the tool is set as the current tool."""
        pass

    def deactivate(self):
        """Called when another tool is set as the current tool."""
        pass

    def mousePressEvent(self, event):
        pass

    def mouseReleaseEvent(self, event):
        pass

    def mouseMoveEvent(self, event):
        pass

    def wheelEvent(self, event):
        """
        Default wheel event implementation for zooming.
        Can be overridden by specialized zoom tools if needed.
        """
        if self._canvas.extent.isEmpty():
            return

        # Map pixel location to world coordinates
        settings = self._canvas.get_settings()
        world_pos = settings.deviceToWorld().map(QPointF(event.position()))
        
        # Calculate zoom factor
        angle = event.angleDelta().y()
        zoom_factor = getattr(self._canvas, 'zoom_factor', 1.15)
        factor = 1.0 / zoom_factor if angle > 0 else zoom_factor
        
        # Zoom extent around the mouse cursor
        new_width = self._canvas.extent.width() * factor
        new_height = self._canvas.extent.height() * factor
        
        rel_x = (world_pos.x() - self._canvas.extent.left()) / self._canvas.extent.width()
        rel_y = (world_pos.y() - self._canvas.extent.top()) / self._canvas.extent.height()
        
        new_left = world_pos.x() - rel_x * new_width
        new_top = world_pos.y() - rel_y * new_height
        
        self._canvas.extent = QRectF(new_left, new_top, new_width, new_height)
        self._canvas.refresh()


class MapToolPan(MapTool):
    """
    Tool for panning the map by dragging the mouse.
    """
    def __init__(self, canvas):
        super().__init__(canvas)
        self._dragging = False
        self._last_mouse_pos = None

    def activate(self):
        self.canvas().setCursor(Qt.ArrowCursor)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self._dragging = True
            self._last_mouse_pos = event.pos()
            self.canvas().setCursor(Qt.ClosedHandCursor)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            self._dragging = False
            self._last_mouse_pos = None
            self.canvas().setCursor(Qt.ArrowCursor)

    def mouseMoveEvent(self, event):
        if self._dragging and self._last_mouse_pos:
            delta = event.pos() - self._last_mouse_pos
            self._last_mouse_pos = event.pos()
            
            settings = self.canvas().get_settings()
            transform = settings.deviceToWorld()
            
            # Map pixel delta to world delta
            p1 = transform.map(QPointF(0, 0))
            p2 = transform.map(QPointF(delta.x(), delta.y()))
            world_delta = p2 - p1
            
            self.canvas().extent.translate(-world_delta.x(), -world_delta.y())
            self.canvas().refresh()

from PySide6.QtWidgets import QGraphicsView, QGraphicsScene, QGraphicsPixmapItem
from PySide6.QtCore import Qt, QPointF, QRectF, Signal, QThreadPool
from PySide6.QtGui import QPainter, QImage, QPixmap
import os

from engine.core.display.base.map_settings import MapSettings
from engine.core.display.pipeline.renderer_job import MapRendererJob

class MapCanvas(QGraphicsView):
    """
    Premium Map Canvas widget inheriting QGraphicsView.
    Uses MapSettings and MapRendererJob for modular, thread-safe rendering.
    """
    coordinates_changed = Signal(float, float) # Emits (x, y) coordinates in canvas CRS
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.scene = QGraphicsScene(self)
        self.setScene(self.scene)
        
        # Setup view performance & navigation settings
        self.setRenderHints(QPainter.Antialiasing | QPainter.SmoothPixmapTransform)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        
        # Pixmap item to display the rendered map
        self.pixmap_item = QGraphicsPixmapItem()
        self.scene.addItem(self.pixmap_item)
        
        self.layers = []
        self._current_job = None
        self.extent = QRectF() 
        self.canvas_crs = "EPSG:3857" # Web Mercator standard for display
        
        # Navigation helpers
        self.zoom_factor = 1.15
        self._map_tool = None
        
    def set_map_tool(self, tool):
        """Sets the current map tool for interaction."""
        if self._map_tool:
            self._map_tool.deactivate()
        self._map_tool = tool
        if self._map_tool:
            self._map_tool.activate()

    def map_tool(self):
        """Returns the current map tool."""
        return self._map_tool

    def add_layer(self, layer):
        """Adds a MapLayer to the internal list and refreshes."""
        self.layers.append(layer)
        if self.extent.isEmpty() and layer.extent:
            # First layer defines initial extent
            self.extent = QRectF(layer.extent)
        self.refresh()
        
    def remove_layer(self, layer_id: str):
        """Removes a layer by its ID and refreshes."""
        self.layers = [l for l in self.layers if l.id != layer_id]
        self.refresh()

    def refresh(self):
        """Aborts existing rendering job and spawns a new MapRendererJob."""
        if self._current_job:
            self._current_job.cancel()
            
        if not self.layers or self.extent.isEmpty():
            self.pixmap_item.setPixmap(QPixmap())
            return

        settings = self.get_settings()
        
        self._current_job = MapRendererJob(settings)
        self._current_job.signals.finished.connect(self._on_render_finished)
        QThreadPool.globalInstance().start(self._current_job)

    def _on_render_finished(self, image: QImage):
        """Updates the pixmap item with the new rendered image."""
        self.pixmap_item.setPixmap(QPixmap.fromImage(image))
        self.pixmap_item.setPos(0, 0)
        self.scene.setSceneRect(0, 0, image.width(), image.height())
        self.resetTransform()
        self._current_job = None

    def zoom_to_extent(self, rect: QRectF):
        """Fit canvas viewport around a bounding rect and re-render."""
        if not rect.isEmpty():
            self.extent = rect
            self.refresh()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.refresh()

    def get_settings(self):
        """Constructs MapSettings based on current canvas state."""
        settings = MapSettings()
        settings.layers = self.layers
        settings.extent = self.extent
        settings.output_size = self.viewport().size()
        settings.destination_crs = self.canvas_crs
        return settings

    # Navigation Event Overrides
    def wheelEvent(self, event):
        if self._map_tool:
            self._map_tool.wheelEvent(event)
        else:
            super().wheelEvent(event)

    def mousePressEvent(self, event):
        if self._map_tool:
            self._map_tool.mousePressEvent(event)
        else:
            super().mousePressEvent(event)

    def mouseReleaseEvent(self, event):
        if self._map_tool:
            self._map_tool.mouseReleaseEvent(event)
        else:
            super().mouseReleaseEvent(event)

    def mouseMoveEvent(self, event):
        if self._map_tool:
            self._map_tool.mouseMoveEvent(event)
        else:
            super().mouseMoveEvent(event)
            
        # Emit coordinates for status bar
        settings = self.get_settings()
        world_pos = settings.deviceToWorld().map(QPointF(event.pos()))
        self.coordinates_changed.emit(world_pos.x(), world_pos.y())

from PySide6.QtWidgets import QGraphicsView, QGraphicsScene, QGraphicsPixmapItem, QApplication
from PySide6.QtCore import Qt, QPointF, QRectF, Signal, QThreadPool, QTimer
from PySide6.QtGui import QPainter, QImage, QPixmap
import os

from core.qgsmapsettings import QgsMapSettings as MapSettings
from gui.qgsmaprendererjob import QgsMapRendererJob as MapRendererJob

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

        self._layers_list = []
        self._current_job = None
        self._extent_rect = QRectF()
        self.canvas_crs = "EPSG:3857" # Web Mercator standard for display

        # Debounce timer for refresh — prevents canceling in-flight renders
        self._refresh_timer = QTimer(self)
        self._refresh_timer.setSingleShot(True)
        self._refresh_timer.setInterval(50)
        self._refresh_timer.timeout.connect(self._do_refresh)

        # Navigation helpers
        self.zoom_factor = 1.15
        self._map_tool = None
        self._in_resize_event = False  # Flag to prevent refresh loop from resizeEvent
        
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

    def layers(self):
        """Returns the list of layers (QgsMapCanvas-compatible method)."""
        return self._layers_list

    def setLayers(self, layer_list):
        """Sets the list of layers (QgsMapCanvas-compatible method)."""
        self._layers_list = layer_list

    def setExtent(self, extent):
        """Sets the canvas extent (QgsMapCanvas-compatible method)."""
        if hasattr(extent, 'xMinimum'):
            self._extent_rect = QRectF(extent.xMinimum(), extent.yMinimum(), extent.width(), extent.height())
        else:
            self._extent_rect = extent

    def extent(self):
        """Returns the canvas extent (QgsMapCanvas-compatible method)."""
        return self._extent_rect

    def add_layer(self, layer):
        """Adds a MapLayer to the internal list and refreshes."""
        self._layers_list.append(layer)
        if self._extent_rect.isEmpty() and layer.extent:
            # First layer defines initial extent
            # QgsRectangle (GIS coords: Y up) -> QRectF (Qt coords: Y down)
            ext = layer.extent
            self._extent_rect = QRectF(ext.xMinimum(), ext.yMaximum(), ext.width(), ext.height())
        self.refresh()

    def remove_layer(self, layer_id: str):
        """Removes a layer by its ID and refreshes."""
        self._layers_list = [l for l in self._layers_list if l.id != layer_id]
        self.refresh()

    def refresh(self):
        """Debounced refresh — coalesces rapid calls (resize events) into one render."""
        if not self._layers_list or self._extent_rect.isEmpty():
            if self._current_job:
                self._current_job.cancel()
                self._current_job = None
            self.pixmap_item.setPixmap(QPixmap())
            return
        # Restart debounce timer — last call wins
        self._refresh_timer.start()

    def _do_refresh(self):
        """Actually spawns a new MapRendererJob."""
        # Check for recursive refresh loop
        if self._in_resize_event:
            from core.logger import log_debug
            log_debug("MapCanvas: Skipping refresh during resize event")
            return

        if self._current_job:
            # Cancel the old job but DON'T wait for it - just start a new one
            # The old job will check _is_canceled and exit gracefully
            self._current_job.cancel()
            # Don't waitForFinished() as it blocks the main thread!
            # Instead, just clear the reference and start a new job
            self._current_job = None

        settings = self.get_settings()

        self._current_job = MapRendererJob(settings)
        # QueuedConnection ensures _on_render_finished is invoked on the GUI
        # thread, which is required because QPixmap can only be created there.
        self._current_job.signals.finished.connect(
            self._on_render_finished, Qt.QueuedConnection
        )
        QThreadPool.globalInstance().start(self._current_job)

    def _on_render_finished(self, image: QImage):
        """Updates the pixmap item with the new rendered image."""
        # Guard: ignore signals from stale/canceled jobs.  A canceled job
        # should never emit, but defense-in-depth for race windows.
        if self._current_job and self._current_job._is_canceled:
            return

        # Guard: check if image is valid before using it
        if image.isNull():
            from core.logger import log_warning
            log_warning("MapCanvas: Received null QImage from renderer, skipping update")
            self._current_job = None
            return

        try:
            # Make a detached copy — the QImage produced by the renderer may
            # reference a numpy buffer that can be garbage-collected.
            safe_image = image.copy()
            if safe_image.isNull():
                log_warning("MapCanvas: Image copy resulted in null QImage")
                self._current_job = None
                return

            self.pixmap_item.setPixmap(QPixmap.fromImage(safe_image))
            self.pixmap_item.setPos(0, 0)

            # Only update scene rect if we're not in a resize event to prevent loops
            if not self._in_resize_event:
                current_rect = self.scene.sceneRect()
                new_rect = QRectF(0, 0, safe_image.width(), safe_image.height())
                # Only update if the size actually changed to prevent unnecessary events
                if current_rect.size() != new_rect.size():
                    self.scene.setSceneRect(new_rect)
                # Don't call resetTransform() as it can trigger resize events
        except Exception as e:
            from core.logger import log_error
            log_error(f"MapCanvas: Error updating display: {e}")
        finally:
            self._current_job = None

    def zoom_to_extent(self, rect):
        """Fit canvas viewport around a bounding rect and re-render."""
        if not rect.isEmpty():
            if hasattr(rect, 'xMinimum'):
                # QgsRectangle (GIS coords: Y up) -> QRectF (Qt coords: Y down)
                # QRectF y position is the TOP, so we use yMaximum
                self._extent_rect = QRectF(rect.xMinimum(), rect.yMaximum(), rect.width(), rect.height())
            else:
                # QRectF
                self._extent_rect = rect
            self.refresh()

    def resizeEvent(self, event):
        self._in_resize_event = True
        super().resizeEvent(event)
        self.refresh()
        self._in_resize_event = False

    def get_settings(self):
        """Constructs MapSettings based on current canvas state."""
        from core.qgsrectangle import QgsRectangle
        settings = MapSettings()
        settings.layers = self._layers_list
        # Convert QRectF to QgsRectangle for QgsMapSettings
        # QRectF uses Qt coords (Y down), QgsRectangle uses GIS coords (Y up)
        # We stored QgsRectangle as QRectF(xmin, ymax, width, height)
        # So to convert back: ymin = top - height (because Qt Y increases downward)
        ext = self._extent_rect
        settings.extent = QgsRectangle(ext.left(), ext.top() - ext.height(), ext.right(), ext.top())
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

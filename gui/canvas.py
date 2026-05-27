"""
C++ QgsMapCanvas wrapper - wraps the QGIS C++ map canvas for use in Python.

This replaces the pure-Python QGraphicsView-based MapCanvas implementation
with the native QGIS C++ rendering engine.
"""

from PySide6.QtCore import QObject, Signal, QPointF, QRectF, QSize, Qt
from PySide6.QtGui import QImage, QPainter
import _antigravity_core as core

# Import coordinate reference system
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem
from core.qgsrectangle import QgsRectangle


class _SignalEmitter(QObject):
    """Helper class to provide signal connectivity for the wrapper."""
    coordinates_changed = Signal(float, float)


class MapCanvas:
    """
    Wrapper around C++ QgsMapCanvas.

    Provides the same interface as the old Python MapCanvas for compatibility
    with existing code (main.py, tools, etc.).

    Signal compatibility: coordinates_changed is exposed like the old Python canvas.
    """

    def __init__(self, parent=None):
        """Create the C++ QgsMapCanvas instance."""
        self._canvas = core.QgsMapCanvas()
        self._layers_list = []  # Track layers in Python (for compatibility)
        self.canvas_crs = "EPSG:3857"

        # Create signal emitter for coordinate updates to match old Python canvas interface
        self._signal_emitter = _SignalEmitter()
        self.coordinates_changed = self._signal_emitter.coordinates_changed

        # Connect C++ signal to our Python signal
        # The C++ signal passes QgsPointXY, we convert to (x, y)
        try:
            self._canvas.xyCoordinates.connect(self._on_xy_coordinates)
        except AttributeError:
            pass  # Signal not connected in test mode

        # Setup refresh timer for image display updates
        from PySide6.QtCore import QTimer
        self._refresh_timer = QTimer()
        self._refresh_timer.timeout.connect(self._update_display_image)
        self._refresh_timer.setInterval(1000)  # Update every second

    def _on_xy_coordinates(self, point):
        """Convert C++ QgsPointXY signal to Python (x, y) signal."""
        if point:
            self.coordinates_changed.emit(point.x(), point.y())

    @property
    def qgs_canvas(self):
        """Access the underlying C++ QgsMapCanvas for advanced usage."""
        return self._canvas

    def set_map_tool(self, tool):
        """Set the current map tool (QgsMapToolPan, QgsMapToolZoom, etc.)."""
        if hasattr(tool, '_qgs_tool'):
            # It's a wrapped C++ tool
            # C++ setMapTool signature: setMapTool(QgsMapTool* mapTool, bool clean = false)
            self._canvas.setMapTool(tool._qgs_tool, False)
        else:
            # Legacy Python tool - not supported with C++ canvas
            raise TypeError("Only C++ QgsMapTool subclasses are supported")

    def map_tool(self):
        """Get the current map tool."""
        return self._canvas.mapTool()

    def layers(self):
        """Return the list of layers."""
        return self._layers_list

    def setLayers(self, layer_list):
        """
        Set the list of layers to display.

        Accepts either Python QgsRasterLayer/QgsVectorLayer objects
        or their C++ equivalents.
        """
        # Convert Python layers to C++ layers if needed
        cxx_layers = []
        for layer in layer_list:
            if hasattr(layer, '_qgs_layer'):
                # It's a wrapped Python layer
                cxx_layers.append(layer._qgs_layer)
            else:
                # It's already a C++ layer (from _antigravity_core)
                cxx_layers.append(layer)

        self._layers_list = layer_list
        self._canvas.setLayers(cxx_layers)

    def setExtent(self, extent):
        """
        Set the visible extent.

        Accepts Python QgsRectangle or QRectF.
        """
        if hasattr(extent, 'xMinimum'):
            # QgsRectangle (Python wrapper)
            rect = core.QgsRectangle(
                extent.xMinimum(), extent.yMinimum(),
                extent.xMaximum(), extent.yMaximum()
            )
            self._canvas.setExtent(rect)
        else:
            # QRectF - convert to QgsRectangle
            rect = core.QgsRectangle(
                extent.left(), extent.top(),
                extent.right(), extent.bottom()
            )
            self._canvas.setExtent(rect)

    def extent(self):
        """Return the current visible extent as QRectF (Python compatibility)."""
        ext = self._canvas.extent()
        if ext:
            return QRectF(ext.xMinimum(), ext.yMinimum(),
                         ext.width(), ext.height())
        return QRectF()

    def add_layer(self, layer):
        """Add a layer and refresh the canvas."""
        self._layers_list.append(layer)
        # Refresh the layer list
        self.setLayers(self._layers_list)
        # Explicitly trigger refresh to update display
        self.refresh()

    def remove_layer(self, layer_id: str):
        """Remove a layer by ID and refresh."""
        self._layers_list = [l for l in self._layers_list if l.id() != layer_id]
        self.setLayers(self._layers_list)

    def refresh(self):
        """Trigger a canvas refresh."""
        self._canvas.refresh()

    def zoom_to_extent(self, rect):
        """
        Zoom to fit the given rectangle.

        Accepts QgsRectangle or QRectF.
        """
        if hasattr(rect, 'xMinimum'):
            # QgsRectangle
            qgs_rect = core.QgsRectangle(
                rect.xMinimum(), rect.yMinimum(),
                rect.xMaximum(), rect.yMaximum()
            )
            self._canvas.setExtent(qgs_rect)
        else:
            # QRectF
            qgs_rect = core.QgsRectangle(
                rect.left(), rect.top(),
                rect.right(), rect.bottom()
            )
            self._canvas.setExtent(qgs_rect)
        self.refresh()

    def zoomIn(self):
        """Zoom in."""
        self._canvas.zoomIn()

    def zoomOut(self):
        """Zoom out."""
        self._canvas.zoomOut()

    def scale(self):
        """Return the current map scale."""
        return self._canvas.scale()

    def magnificationFactor(self):
        """Return the current magnification factor."""
        return self._canvas.magnificationFactor()

    def set_destination_crs(self, crs_auth_id):
        """Set the destination CRS (e.g., 'EPSG:3857')."""
        crs = QgsCoordinateReferenceSystem(crs_auth_id)
        if hasattr(crs, '_qgs_crs'):
            self._canvas.setDestinationCrs(crs._qgs_crs)

    def saveAsImage(self, path):
        """
        Render the current canvas to an image file.

        This uses the synchronous C++ rendering (QgsMapRendererCustomPainterJob),
        avoiding event loop issues.
        """
        self._canvas.saveAsImage(path)

    def resize(self, width, height):
        """Resize the canvas (for widget embedding)."""
        # C++ QgsMapCanvas is a QWidget, so it has resize()
        if hasattr(self._canvas, 'resize'):
            self._canvas.resize(width, height)

    def size(self):
        """Return the canvas size."""
        if hasattr(self._canvas, 'size'):
            return self._canvas.size()
        return QSize(800, 600)

    def show(self):
        """Show the canvas widget."""
        if hasattr(self._canvas, 'show'):
            self._canvas.show()

    def setParent(self, parent):
        """Set the parent widget."""
        if hasattr(self._canvas, 'setParent'):
            self._canvas.setParent(parent)

    def set_display_widget(self, label):
        """Set QLabel widget for displaying rendered map images."""
        self._display_label = label

    def start_refresh_timer(self):
        """Start automatic refresh for image display."""
        if hasattr(self, '_refresh_timer'):
            self._refresh_timer.start()

    def _update_display_image(self):
        """Update display label with rendered map image."""
        if not self._layers_list or not hasattr(self, '_display_label'):
            return

        try:
            import tempfile
            import os
            from PySide6.QtGui import QPixmap

            # Render to temp file
            temp_path = tempfile.mktemp(suffix=".png")
            self.saveAsImage(temp_path)

            # Load and display
            if os.path.exists(temp_path):
                pixmap = QPixmap(temp_path)
                if not pixmap.isNull():
                    # Scale to fit the label while keeping aspect ratio
                    scaled = pixmap.scaled(
                        self._display_label.size(),
                        Qt.KeepAspectRatio,
                        Qt.SmoothTransformation
                    )
                    self._display_label.setPixmap(scaled)
                os.unlink(temp_path)

        except Exception as e:
            from core.logger import log_debug
            log_debug(f"Display update failed: {e}")

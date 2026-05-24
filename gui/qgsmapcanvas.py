from PySide6.QtWidgets import QGraphicsView, QGraphicsScene
from PySide6.QtCore import Qt, QRectF, Signal, QThreadPool, QSize
from PySide6.QtGui import QPainter, QImage

from core.qgsmapsettings import QgsMapSettings
from core.qgsrectangle import QgsRectangle
from core.qgspointxy import QgsPointXY
from core.qgsmaptopixel import QgsMapToPixel
from gui.qgsmapcanvasmap import QgsMapCanvasMap
from gui.qgsmaprendererjob import QgsMapRendererJob


class QgsMapCanvas(QGraphicsView):
    """QGraphicsView-based map canvas matching QGIS C++ architecture."""

    coordinates_changed = Signal(float, float)
    extentsChanged = Signal()
    renderComplete = Signal()
    layersChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._scene = QGraphicsScene(self)
        self.setScene(self._scene)

        # Map image item
        self._map_item = QgsMapCanvasMap(self._scene)

        # State
        self._layers = []
        self._extent = QgsRectangle()
        self._canvas_crs = "EPSG:3857"
        self._map_tool = None
        self._current_job = None
        self._render_generation = 0
        self._zoom_factor = 1.15
        self._scale_bar_settings = None

        # Viewport settings
        self.setRenderHints(QPainter.Antialiasing | QPainter.SmoothPixmapTransform)
        self.setViewportUpdateMode(QGraphicsView.MinimalViewportUpdate)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)

        # CRS handling
        try:
            from core.qgsproject import GISProject
            self._canvas_crs = GISProject.instance().crs()
            GISProject.instance().crsChanged.connect(self._set_canvas_crs)
        except Exception:
            pass

    # --- Extent ---
    def setExtent(self, extent: QgsRectangle):
        self._extent = extent
        self._update_scene_rect()
        self.extentsChanged.emit()

    def extent(self) -> QgsRectangle:
        return self._extent

    # --- Layers ---
    def setLayers(self, layers: list):
        self._layers = layers
        self.layersChanged.emit()

    def layers(self) -> list:
        return self._layers

    # --- Map tool ---
    def setMapTool(self, tool):
        if self._map_tool:
            self._map_tool.deactivate()
        self._map_tool = tool
        if self._map_tool:
            self._map_tool.activate()

    def mapTool(self):
        return self._map_tool

    # --- Scale bar ---
    def setScaleBarSettings(self, settings):
        """Set scale bar decoration settings (QgsScaleBarSettings or None)."""
        self._scale_bar_settings = settings

    def scaleBarSettings(self):
        """Return current scale bar settings, or None."""
        return self._scale_bar_settings

    # --- Map settings ---
    def mapSettings(self) -> QgsMapSettings:
        settings = QgsMapSettings()
        settings.layers = self._layers
        settings.extent = self._extent  # QgsRectangle directly, not QRectF
        settings.output_size = self.viewport().size()
        settings.destination_crs = self._canvas_crs
        return settings

    def mapToPixel(self) -> QgsMapToPixel:
        return QgsMapToPixel.fromSettings(self._extent, self.viewport().size())

    # --- Refresh ---
    def refresh(self):
        if self._current_job:
            self._current_job.cancel()
            self._current_job = None

        if not self._layers or self._extent.isEmpty():
            self._map_item.clear()
            return

        self._render_generation += 1
        generation = self._render_generation

        # Show fast low-res preview immediately
        self.renderPreview()

        # Start full-res render in background
        settings = self.mapSettings()
        job = QgsMapRendererJob(settings)
        job.signals.finished.connect(lambda img, gen=generation: self._on_render_finished(img, gen))
        self._current_job = job
        QThreadPool.globalInstance().start(job)

    def renderPreview(self):
        """Render a tiny preview image synchronously for instant feedback."""
        PREVIEW_MAX = 256
        w = self.viewport().width()
        h = self.viewport().height()
        if w <= 0 or h <= 0 or self._extent.isEmpty():
            return
        scale = PREVIEW_MAX / max(w, h)
        pw, ph = max(1, int(w * scale)), max(1, int(h * scale))

        from PySide6.QtCore import QSize as QSize2
        preview_settings = QgsMapSettings()
        preview_settings.layers = self._layers
        # Pass QgsRectangle directly — QgsMapToPixel.fromSettings needs
        # .xMinimum()/.yMaximum() which QRectF does not provide.
        preview_settings.extent = self._extent
        preview_settings.output_size = QSize2(pw, ph)
        preview_settings.destination_crs = self._canvas_crs

        preview_job = QgsMapRendererJob(preview_settings)
        preview_job.run()  # Synchronous — tiny image, ~ms
        if hasattr(preview_job, '_last_image') and preview_job._last_image and not preview_job._last_image.isNull():
            self._map_item.setImage(preview_job._last_image)

    def _on_render_finished(self, image: QImage, generation: int):
        if generation != self._render_generation:
            return
        # Draw scale bar on the image if configured
        if self._scale_bar_settings and not image.isNull():
            from PySide6.QtGui import QPainter
            from PySide6.QtCore import QPointF
            from core.scalebar.qgsscalebarrenderer import QgsScaleBarRenderer
            painter = QPainter(image)
            renderer = QgsScaleBarRenderer(self._scale_bar_settings)
            # Position: bottom-left with 10px margin
            x = 10
            y = image.height() - 10
            # Calculate scale denominator from extent and image size
            scale_denom = self._calculate_scale_denominator(image.size())
            renderer.render(painter, scale_denom, QPointF(x, y))
            painter.end()
        self._map_item.setImage(image)
        self._current_job = None
        self.renderComplete.emit()

    def _calculate_scale_denominator(self, image_size):
        """Calculate the map scale denominator from extent and image size."""
        ext = self._extent
        if ext.isEmpty() or image_size.width() == 0:
            return 1
        # Scale denom = ground distance / paper distance
        # Using 96 DPI: 1 pixel = 25.4/96 mm
        mm_per_pixel = 25.4 / 96
        ground_width_mm = ext.width() * 1000  # assuming extent is in meters
        image_width_mm = image_size.width() * mm_per_pixel
        return ground_width_mm / image_width_mm

    # --- CRS ---
    def _set_canvas_crs(self, new_crs: str):
        old_crs = self._canvas_crs
        if old_crs == new_crs:
            return
        self._canvas_crs = new_crs
        if not self._extent.isEmpty():
            from core.qgscoordinatetransform import QgsCoordinateTransform
            try:
                transformer = QgsCoordinateTransform(old_crs, new_crs)
                xmin, ymin, xmax, ymax = transformer.transform_bounds(
                    self._extent.left(), self._extent.bottom(),
                    self._extent.right(), self._extent.top()
                )
                self.setExtent(QgsRectangle(xmin, ymin, xmax, ymax))
            except Exception:
                pass
        self.refresh()

    # --- Scene ---
    def _update_scene_rect(self):
        if not self._extent.isEmpty():
            r = self._extent
            self._scene.setSceneRect(QRectF(r.xMinimum(), r.yMinimum(), r.width(), r.height()))

    # --- Events ---
    def wheelEvent(self, event):
        if self._map_tool:
            self._map_tool.wheelEvent(event)
        else:
            super().wheelEvent(event)

    def mousePressEvent(self, event):
        if self._map_tool:
            self._map_tool.mousePressEvent(event)
        super().mousePressEvent(event)

    def mouseReleaseEvent(self, event):
        if self._map_tool:
            self._map_tool.mouseReleaseEvent(event)
        super().mouseReleaseEvent(event)

    def mouseMoveEvent(self, event):
        if self._map_tool:
            self._map_tool.mouseMoveEvent(event)
        super().mouseMoveEvent(event)
        # Emit coordinates
        settings = self.mapSettings()
        world_pos = settings.deviceToWorld().map(QPointF(event.pos()))
        self.coordinates_changed.emit(world_pos.x(), world_pos.y())

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.refresh()

    # --- Backward compatibility ---
    def add_layer(self, layer):
        from core.qgsproject import GISProject
        GISProject.instance().addMapLayers([layer])
        GISProject.instance().layerTreeRoot().addLayer(layer)
        if self._extent.isEmpty() and layer.extent:
            self.setExtent(layer.extent)

    def remove_layer(self, layer_id: str):
        from core.qgsproject import GISProject
        GISProject.instance().removeMapLayers([layer_id])
        root = GISProject.instance().layerTreeRoot()
        for child in list(root.children()):
            if child.nodeType() == "layer" and child.layer_id == layer_id:
                root.removeChildNode(child)
                break

    # --- Property aliases for backward compat ---
    @property
    def extent_as_qrectf(self):
        e = self._extent
        return QRectF(e.xMinimum(), e.yMinimum(), e.width(), e.height())


MapCanvas = QgsMapCanvas

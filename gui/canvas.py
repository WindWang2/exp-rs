from PySide6.QtWidgets import QGraphicsView, QGraphicsScene, QGraphicsPixmapItem, QGraphicsPathItem
from PySide6.QtCore import Qt, QPointF, QRectF, Signal
from PySide6.QtGui import QPainter, QImage, QPixmap, QPen, QColor, QBrush, QPainterPath
import numpy as np
import os
from shapely.geometry import Point, LineString, Polygon, MultiPolygon
from engine.core.reader import GeospatialReader
from engine.core.projection import CRSTransformer

class MapCanvas(QGraphicsView):
    """
    Premium Map Canvas widget inheriting QGraphicsView.
    Displays raster overlays (NumPy-to-QImage overviews) and vector overlays (QGraphicsPathItem).
    Supports smooth wheel-zooming, middle-click panning, and dynamic hover coordinate display.
    """
    coordinates_changed = Signal(float, float) # Emits (x, y) coordinates in canvas CRS
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.scene = QGraphicsScene(self)
        self.setScene(self.scene)
        
        # Setup view performance & navigation settings
        self.setRenderHints(QPainter.Antialiasing | QPainter.SmoothPixmapTransform)
        self.setDragMode(QGraphicsView.ScrollHandDrag)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setTransformationAnchor(QGraphicsView.AnchorUnderMouse)
        
        # Tracks layer graphics items: {layer_id: [graphics_items]}
        self.layer_items = {}
        self.canvas_crs = "EPSG:3857" # Web Mercator standard for display
        
        # Navigation helpers
        self.zoom_factor = 1.15
        
    def add_raster_layer(self, layer_id: str, file_path: str) -> QRectF:
        """Loads a raster file, downsamples it, normalizes bands, and draws as a Pixmap."""
        reader = GeospatialReader(file_path)
        metadata = reader.metadata
        
        # Reproject bounds to Canvas CRS
        transformer = CRSTransformer(metadata["crs"], self.canvas_crs)
        l, b, r, t = transformer.transform_bounds(
            metadata["bounds"]["left"],
            metadata["bounds"]["bottom"],
            metadata["bounds"]["right"],
            metadata["bounds"]["top"]
        )
        
        # Downsample for canvas display (to a max size of 2048x2048)
        max_dim = max(metadata["width"], metadata["height"])
        scale_factor = 1
        if max_dim > 2048:
            scale_factor = int(max_dim / 2048)
            
        # Read bands for display
        band_count = metadata["count"]
        if band_count >= 3:
            # RGB Composite (assume bands 1, 2, 3 represent R, G, B)
            r_band = reader.read_raster_band(1, scale_factor)
            g_band = reader.read_raster_band(2, scale_factor)
            b_band = reader.read_raster_band(3, scale_factor)
            
            # Normalize helper
            def norm(arr):
                amin, amax = arr.min(), arr.max()
                if amax - amin > 0:
                    return ((arr - amin) / (amax - amin) * 255).astype(np.uint8)
                return np.zeros_like(arr, dtype=np.uint8)
                
            r_norm = norm(r_band)
            g_norm = norm(g_band)
            b_norm = norm(b_band)
            
            # Stack into RGB array
            h, w = r_norm.shape
            rgb = np.dstack((r_norm, g_norm, b_norm))
            
            # Convert to QImage (making a hard copy of rgb data to prevent garbage collection crashes)
            rgb_data = np.ascontiguousarray(rgb)
            q_img = QImage(rgb_data.data, w, h, 3 * w, QImage.Format_RGB888)
            # Create a detached copy of the image data to ensure memory safety in Qt loops
            q_img_copy = q_img.copy()
        else:
            # Grayscale for single-band rasters
            band = reader.read_raster_band(1, scale_factor)
            amin, amax = band.min(), band.max()
            if amax - amin > 0:
                norm_band = ((band - amin) / (amax - amin) * 255).astype(np.uint8)
            else:
                norm_band = np.zeros_like(band, dtype=np.uint8)
                
            h, w = norm_band.shape
            gray = np.dstack((norm_band, norm_band, norm_band))
            gray_data = np.ascontiguousarray(gray)
            q_img = QImage(gray_data.data, w, h, 3 * w, QImage.Format_RGB888)
            q_img_copy = q_img.copy()
            
        # Convert QImage to QPixmap and add to scene
        pixmap = QPixmap.fromImage(q_img_copy)
        pixmap_item = QGraphicsPixmapItem(pixmap)
        
        # Position and scale the Pixmap in Scene coordinates
        rect_width = r - l
        rect_height = t - b
        
        pixmap_item.setPos(l, -t)
        scale_x = rect_width / w
        scale_y = rect_height / h
        pixmap_item.setScale(scale_x) # Assume isotropic square pixels for simplicity
        
        self.scene.addItem(pixmap_item)
        self.layer_items[layer_id] = [pixmap_item]
        
        # Return reprojected scene bounds for zooming
        return QRectF(l, -t, rect_width, rect_height)

    def add_vector_layer(self, layer_id: str, file_path: str) -> QRectF:
        """Loads a vector file, reprojects geometries, and adds them as PathItems."""
        reader = GeospatialReader(file_path)
        metadata = reader.metadata
        
        transformer = CRSTransformer(metadata["crs"], self.canvas_crs)
        l, b, r, t = transformer.transform_bounds(
            metadata["bounds"]["left"],
            metadata["bounds"]["bottom"],
            metadata["bounds"]["right"],
            metadata["bounds"]["top"]
        )
        
        features = reader.read_vector_features()
        items = []
        
        # Formal ArcGIS corporate blue styling
        pen_color = QColor("#007ac2") # ArcGIS Blue
        brush_color = QColor(0, 122, 194, 25) # Semi-transparent light blue fill
        
        pen = QPen(pen_color, 2)
        pen.setCosmetic(True) # Keep thick line perfectly sharp at any zoom level
        brush = QBrush(brush_color)
        
        for feat in features:
            geom_shape = feat["shape"]
            if geom_shape is None:
                continue
                
            # Reproject geometry
            proj_shape = transformer.transform_geometry(geom_shape)
            
            # Build QPainterPath from Shape geometries
            path = QPainterPath()
            if isinstance(proj_shape, Point):
                path.addEllipse(QPointF(proj_shape.x, -proj_shape.y), 4, 4)
            elif isinstance(proj_shape, LineString):
                coords = list(proj_shape.coords)
                if coords:
                    path.moveTo(coords[0][0], -coords[0][1])
                    for x, y in coords[1:]:
                        path.lineTo(x, -y)
            elif isinstance(proj_shape, (Polygon, MultiPolygon)):
                def add_polygon(poly):
                    ext_coords = list(poly.exterior.coords)
                    if ext_coords:
                        path.moveTo(ext_coords[0][0], -ext_coords[0][1])
                        for x, y in ext_coords[1:]:
                            path.lineTo(x, -y)
                        path.closeSubpath()
                    
                    # Paint holes (interiors)
                    for interior in poly.interiors:
                        int_coords = list(interior.coords)
                        if int_coords:
                            path.moveTo(int_coords[0][0], -int_coords[0][1])
                            for x, y in int_coords[1:]:
                                path.lineTo(x, -y)
                            path.closeSubpath()
                            
                if isinstance(proj_shape, Polygon):
                    add_polygon(proj_shape)
                else:
                    for poly in proj_shape.geoms:
                        add_polygon(poly)
                        
            path_item = QGraphicsPathItem(path)
            path_item.setPen(pen)
            path_item.setBrush(brush)
            self.scene.addItem(path_item)
            items.append(path_item)
            
        self.layer_items[layer_id] = items
        return QRectF(l, -t, r - l, t - b)

    def remove_layer(self, layer_id: str):
        """Safely removes all graphics elements associated with a layer."""
        if layer_id in self.layer_items:
            for item in self.layer_items[layer_id]:
                self.scene.removeItem(item)
            del self.layer_items[layer_id]

    def zoom_to_extent(self, rect: QRectF):
        """Fit canvas viewport around a bounding rect with a slight margin."""
        if not rect.isEmpty():
            self.fitInView(rect, Qt.KeepAspectRatio)

    # Navigation Event Overrides
    def wheelEvent(self, event):
        angle = event.angleDelta().y()
        factor = self.zoom_factor if angle > 0 else (1.0 / self.zoom_factor)
        self.scale(factor, factor)

    def mouseMoveEvent(self, event):
        super().mouseMoveEvent(event)
        # Map pixel location to scene coordinates
        scene_pos = self.mapToScene(event.pos())
        self.coordinates_changed.emit(scene_pos.x(), -scene_pos.y())

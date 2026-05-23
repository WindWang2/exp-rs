from engine.core.display.base.map_layer import MapLayer
from engine.core.display.raster.provider import GDALDataProvider
from PySide6.QtGui import QImage
import numpy as np

class RasterLayer(MapLayer):
    """
    Map layer for displaying raster data.
    """
    def __init__(self, layer_id: str, name: str, uri: str):
        super().__init__(layer_id, name)
        self.provider = GDALDataProvider(uri)
        self.extent = self.provider.extent()
        self.crs = self.provider.reader.metadata.get("crs")

    def draw(self, painter, settings):
        """
        Draws the raster layer using the provided painter and settings.
        """
        if not self.visible or painter is None:
            return

        # 1. Capture extent from settings (if available) or use layer extent
        view_extent = settings.extent if settings.extent else self.extent
        
        # 2. Use the provider to read pixel data
        # (Simplified downsampling logic similar to gui/canvas.py)
        reader = self.provider.reader
        metadata = reader.metadata
        
        max_dim = max(metadata["width"], metadata["height"])
        scale_factor = 1
        if max_dim > 2048:
            scale_factor = int(max_dim / 2048)
            
        band_count = metadata["count"]
        image_to_draw = None
        
        if band_count >= 3:
            # RGB Composite
            r_band = reader.read_raster_band(1, scale_factor)
            g_band = reader.read_raster_band(2, scale_factor)
            b_band = reader.read_raster_band(3, scale_factor)
            
            def norm(arr):
                amin, amax = arr.min(), arr.max()
                if amax - amin > 0:
                    return ((arr - amin) / (amax - amin) * 255).astype(np.uint8)
                return np.zeros_like(arr, dtype=np.uint8)
                
            r_norm = norm(r_band)
            g_norm = norm(g_band)
            b_norm = norm(b_band)
            
            h, w = r_norm.shape
            rgb = np.dstack((r_norm, g_norm, b_norm))
            rgb_data = np.ascontiguousarray(rgb)
            q_img = QImage(rgb_data.data, w, h, 3 * w, QImage.Format_RGB888)
            image_to_draw = q_img.copy()
        else:
            # Grayscale
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
            image_to_draw = q_img.copy()

        if image_to_draw:
            # 3. Draw at correct location
            # Note: For now, we draw at (0,0) or simplified mapping.
            # In a full implementation, we'd use settings.worldToDevice transform.
            
            # Simple opacity support
            if self.opacity < 1.0:
                painter.setOpacity(self.opacity)
            
            painter.drawImage(0, 0, image_to_draw)
            
            if self.opacity < 1.0:
                painter.setOpacity(1.0)

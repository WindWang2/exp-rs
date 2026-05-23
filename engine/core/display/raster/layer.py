from engine.core.display.base.map_layer import MapLayer
from engine.core.display.raster.provider import GDALDataProvider
from PySide6.QtGui import QImage
from PySide6.QtCore import QRectF
import numpy as np
import matplotlib.cm as cm

class RasterLayer(MapLayer):
    """
    Map layer for displaying raster data.
    Supports advanced rendering (Multiband Color, Singleband Gray, Singleband Pseudocolor)
    and On-The-Fly (OTF) reprojection of layer bounding boxes.
    """
    def __init__(self, layer_id: str, name: str, uri: str):
        super().__init__(layer_id, name)
        self.provider = GDALDataProvider(uri)
        ext = self.provider.extent()
        self.crs = self.provider.reader.metadata.get("crs")
        
        # Calculate raw extent in native projection
        self.raw_extent = QRectF(ext["left"], ext["top"], ext["right"] - ext["left"], ext["top"] - ext["bottom"])
        
        # Project extent to Web Mercator (EPSG:3857) for consistent canvas viewport bounds
        if self.crs and self.crs != "EPSG:3857":
            from engine.core.projection import CRSTransformer
            try:
                transformer = CRSTransformer(self.crs, "EPSG:3857")
                xmin, ymin, xmax, ymax = transformer.transform_bounds(ext["left"], ext["bottom"], ext["right"], ext["top"])
                self.extent = QRectF(xmin, ymax, xmax - xmin, ymax - ymin)
            except Exception as e:
                print(f"Error reprojecting raster extent for {name}: {e}")
                self.extent = self.raw_extent
        else:
            self.extent = self.raw_extent

        # Advanced styling and symbology attributes
        band_count = self.provider.reader.metadata.get("count", 1)
        self.render_type = "multiband" if band_count >= 3 else "grayscale"
        self.red_band = 1
        self.green_band = 2 if band_count >= 2 else 1
        self.blue_band = 3 if band_count >= 3 else 1
        self.gray_band = 1
        self.pseudocolor_band = 1
        self.color_ramp = "viridis"
        self.min_val = None
        self.max_val = None

    def draw(self, painter, settings):
        """
        Draws the raster layer using the provided painter and settings,
        supporting custom band configuration, colormaps, and opacity.
        """
        if not self.visible or painter is None:
            return

        # Use the provider to read pixel data
        reader = self.provider.reader
        metadata = reader.metadata
        
        max_dim = max(metadata["width"], metadata["height"])
        scale_factor = 1
        if max_dim > 2048:
            scale_factor = int(max_dim / 2048)
            
        image_to_draw = None

        def stretch(arr, min_v=None, max_v=None):
            amin = float(min_v) if min_v is not None else float(arr.min())
            amax = float(max_v) if max_v is not None else float(arr.max())
            if amax - amin > 0:
                stretched = ((arr.astype(float) - amin) / (amax - amin) * 255.0)
                return np.clip(stretched, 0, 255).astype(np.uint8)
            return np.zeros_like(arr, dtype=np.uint8)

        try:
            if self.render_type == "multiband":
                # RGB Composite
                r_band = reader.read_raster_band(self.red_band, scale_factor)
                g_band = reader.read_raster_band(self.green_band, scale_factor)
                b_band = reader.read_raster_band(self.blue_band, scale_factor)
                
                r_norm = stretch(r_band, self.min_val, self.max_val)
                g_norm = stretch(g_band, self.min_val, self.max_val)
                b_norm = stretch(b_band, self.min_val, self.max_val)
                
                h, w = r_norm.shape
                rgb = np.dstack((r_norm, g_norm, b_norm))
                rgb_data = np.ascontiguousarray(rgb)
                q_img = QImage(rgb_data.data, w, h, 3 * w, QImage.Format_RGB888)
                image_to_draw = q_img.copy()

            elif self.render_type == "grayscale":
                # Singleband Gray
                band = reader.read_raster_band(self.gray_band, scale_factor)
                norm_band = stretch(band, self.min_val, self.max_val)
                
                h, w = norm_band.shape
                gray = np.dstack((norm_band, norm_band, norm_band))
                gray_data = np.ascontiguousarray(gray)
                q_img = QImage(gray_data.data, w, h, 3 * w, QImage.Format_RGB888)
                image_to_draw = q_img.copy()

            elif self.render_type == "pseudocolor":
                # Singleband Pseudocolor using matplotlib color ramps
                band = reader.read_raster_band(self.pseudocolor_band, scale_factor)
                amin = float(self.min_val) if self.min_val is not None else float(band.min())
                amax = float(self.max_val) if self.max_val is not None else float(band.max())
                
                # Normalize to [0.0, 1.0] for colormap
                if amax - amin > 0:
                    norm = np.clip((band.astype(float) - amin) / (amax - amin), 0.0, 1.0)
                else:
                    norm = np.zeros_like(band, dtype=float)
                
                # Retrieve color map from matplotlib
                import matplotlib
                try:
                    cmap = matplotlib.colormaps[self.color_ramp]
                except KeyError:
                    # Fallback to viridis if ramp name is invalid
                    cmap = matplotlib.colormaps["viridis"]
                    
                rgba = cmap(norm)  # returns shape (H, W, 4)
                rgb = (rgba[:, :, :3] * 255.0).astype(np.uint8)
                
                h, w = band.shape
                rgb_data = np.ascontiguousarray(rgb)
                q_img = QImage(rgb_data.data, w, h, 3 * w, QImage.Format_RGB888)
                image_to_draw = q_img.copy()
                
        except Exception as e:
            print(f"Error rendering raster layer {self.name}: {e}")

        if image_to_draw:
            painter.save()
            
            world_to_device = settings.worldToDevice()
            # Map the layer extent to device coordinates
            target_rect = world_to_device.mapRect(self.extent)
            
            if self.opacity < 1.0:
                painter.setOpacity(self.opacity)
            
            painter.drawImage(target_rect, image_to_draw)
            painter.restore()


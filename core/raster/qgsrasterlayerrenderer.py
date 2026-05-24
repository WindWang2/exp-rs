from PySide6.QtGui import QImage
from PySide6.QtCore import QRectF, Qt
import numpy as np

from core.qgsmaplayerrenderer import QgsMapLayerRenderer


class QgsRasterLayerRenderer(QgsMapLayerRenderer):
    """
    Thread-safe, decoupled drawing class for Raster Layers matching QgsRasterLayerRenderer.
    Loads and styles pixel arrays dynamically in the background without locking GUI QObjects.

    The renderer is obtained from the layer's pipe.  If no renderer is available,
    render() returns early (no drawing occurs).
    """
    def __init__(self, layer, settings):
        super().__init__(layer.id)
        self.crs = layer.crs
        self.extent = layer.extent  # Dynamic property evaluated on main thread!
        self.opacity = layer.opacity
        self.visible = layer.visible
        self.file_path = layer.provider.reader.file_path

        # Get the renderer from the pipe (preferred path after rewrite).
        pipe = getattr(layer, '_pipe', None)
        if pipe is not None:
            self.renderer = pipe.renderer()
        else:
            self.renderer = None

    def render(self, painter, settings, renderContext=None):
        if not self.visible or painter is None:
            return

        if self.renderer is None:
            return
            
        from core.qgsreader import GeospatialReader
        import rasterio
        import rasterio.windows
        
        # 1. Open independent reader handle inside render thread
        reader = GeospatialReader(self.file_path)
        
        # 2. Get viewport settings
        view_extent = settings.extent if settings.extent else self.extent
        dest_crs = settings.destination_crs
        
        # 3. Intersect viewport with layer bounds and crop using control points inverse mapping
        with rasterio.open(self.file_path) as src:
            src_crs = src.crs.to_string() if src.crs else None
            
            # Viewport size
            W = settings.output_size.width() if settings.output_size else 1024
            H = settings.output_size.height() if settings.output_size else 768
            
            # Map four screen corners: (0,0), (W,0), (W,H), (0,H) to raster pixel coordinates (col, row)
            dx_c = view_extent.width() / W
            dy_c = view_extent.height() / H
            
            screen_corners = [(0, 0), (W, 0), (W, H), (0, H)]
            raster_corners = []
            
            from core.qgscoordinatetransform import QgsCoordinateTransform
            transformer = None
            if src_crs and dest_crs and src_crs != dest_crs:
                try:
                    transformer = QgsCoordinateTransform(dest_crs, src_crs)
                except Exception as e:
                    print(f"Error creating QgsCoordinateTransform: {e}")
            
            inverse_transform = ~src.transform
            
            for u, v in screen_corners:
                x_canvas = view_extent.left() + u * dx_c
                y_canvas = view_extent.top() - v * dy_c
                
                if transformer:
                    try:
                        x_raster, y_raster = transformer.transform(x_canvas, y_canvas)
                    except Exception:
                        x_raster, y_raster = x_canvas, y_canvas
                else:
                    x_raster, y_raster = x_canvas, y_canvas
                    
                col, row = inverse_transform * (x_raster, y_raster)
                raster_corners.append((col, row))
                
            cols = [pt[0] for pt in raster_corners]
            rows = [pt[1] for pt in raster_corners]
            
            col_min = min(cols)
            col_max = max(cols)
            row_min = min(rows)
            row_max = max(rows)
            
            # Check overlap
            if col_max < 0 or col_min >= src.width or row_max < 0 or row_min >= src.height:
                return # Completely off-screen
                
            # Floor/ceil window boundaries to integer pixels
            col_off = max(0, min(src.width - 1, int(np.floor(col_min))))
            row_off = max(0, min(src.height - 1, int(np.floor(row_min))))
            
            col_max_clp = max(0, min(src.width - 1, int(np.ceil(col_max))))
            row_max_clp = max(0, min(src.height - 1, int(np.ceil(row_max))))
            
            width = max(1, col_max_clp - col_off + 1)
            height = max(1, row_max_clp - row_off + 1)
            
            window = rasterio.windows.Window(col_off, row_off, width, height)
            
            if window.width <= 0 or window.height <= 0:
                return
                
            # 4. Calculate dynamic scale factor based on screen resolution
            # Screen resolution (map units per screen pixel)
            screen_res = view_extent.width() / W
            # Raster resolution (map units per raster pixel)
            raster_res = src.res[0] if src.res else 1.0
            # Downsampling scale ratio
            ratio = screen_res / raster_res
            scale_factor = max(1, int(ratio))
            
            # Solve analytically for the 6 affine warping coefficients
            col_0, row_0 = raster_corners[0] # (0, 0)
            col_W, row_W = raster_corners[1] # (W, 0)
            col_H, row_H = raster_corners[3] # (0, H)
            
            a_0 = (col_0 - col_off) / scale_factor
            a_1 = (col_W - col_0) / (W * scale_factor)
            a_2 = (col_H - col_0) / (H * scale_factor)
            
            b_0 = (row_0 - row_off) / scale_factor
            b_1 = (row_W - row_0) / (W * scale_factor)
            b_2 = (row_H - row_0) / (H * scale_factor)
            
            coeffs_x = np.array([a_0, a_1, a_2], dtype=np.float64)
            coeffs_y = np.array([b_0, b_1, b_2], dtype=np.float64)
            
        # 5. Read the cropped styled image from the decoupled strategy renderer
        try:
            image_to_draw = self.renderer.render(
                reader, scale_factor, window=window,
                out_size=(W, H), coeffs_x=coeffs_x, coeffs_y=coeffs_y
            )
            if image_to_draw:
                painter.save()
                if self.opacity < 1.0:
                    painter.setOpacity(self.opacity)
                    
                # The warped image is already perfectly aligned with the viewport/device grid!
                painter.drawImage(QRectF(0, 0, W, H), image_to_draw)
                painter.restore()
        except Exception as e:
            print(f"Error rendering raster layer in thread: {e}")

RasterLayerRenderer = QgsRasterLayerRenderer

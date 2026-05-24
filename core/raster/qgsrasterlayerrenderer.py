from PySide6.QtGui import QImage
from PySide6.QtCore import QRectF, Qt
import numpy as np
import rasterio
import rasterio.windows

from core.qgsmaplayerrenderer import QgsMapLayerRenderer
from core.logger import log_error, log_warning, log_debug, gdal_lock


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

        # Pre-create coordinate transform on MAIN thread using cache
        # This prevents pyproj C extension segfaults in background threads
        # Direction: dest_crs -> src_crs (for transforming canvas coords to raster coords)
        self._cached_transform = None
        dest_crs = settings.destination_crs if settings else None
        if self.crs and dest_crs and self.crs != dest_crs:
            from core.qgstransformcache import transform_cache
            # Note: get_transform(src, dst) creates transform FROM src TO dst
            # We need FROM dest TO src, so we swap the arguments
            self._cached_transform = transform_cache().get_transform(dest_crs, self.crs)

    def render(self, painter, settings, renderContext=None):
        if not self.visible or painter is None:
            return

        if self.renderer is None:
            return

        try:
            from core.qgsreader import GeospatialReader

            # 1. Open independent reader handle inside render thread
            # NOTE: GeospatialReader.__init__ uses gdal_lock internally.
            try:
                reader = GeospatialReader(self.file_path)
            except Exception as e:
                log_error(f"RasterLayerRenderer: Failed to open {self.file_path}: {e}")
                return

            # 2. Get viewport settings
            view_extent = settings.extent if settings.extent else self.extent
            dest_crs = settings.destination_crs

            # 3. Intersect viewport with layer bounds and crop using control points inverse mapping
            try:
                with gdal_lock:
                    with rasterio.open(self.file_path, sharing=False) as src:
                        src_crs = src.crs.to_string() if src.crs else None

                        # Viewport size
                        W = settings.output_size.width() if settings.output_size else 1024
                        H = settings.output_size.height() if settings.output_size else 768

                        # Map four screen corners: (0,0), (W,0), (W,H), (0,H) to raster pixel coordinates (col, row)
                        dx_c = view_extent.width() / W if W > 0 else 1.0
                        dy_c = view_extent.height() / H if H > 0 else 1.0

                        screen_corners = [(0, 0), (W, 0), (W, H), (0, H)]
                        raster_corners = []

                        # Use pre-created transform from cache (thread-safe)
                        transformer = self._cached_transform

                        try:
                            inverse_transform = ~src.transform
                        except Exception as e:
                            log_error(f"RasterLayerRenderer: Failed to compute inverse transform: {e}")
                            return

                        for u, v in screen_corners:
                            x_canvas = view_extent.left() + u * dx_c
                            y_canvas = view_extent.top() - v * dy_c

                            if transformer:
                                try:
                                    # transformer.transform_xy uses geospatial_lock internally
                                    x_raster, y_raster = transformer.transform_xy(x_canvas, y_canvas)
                                except Exception:
                                    x_raster, y_raster = x_canvas, y_canvas
                            else:
                                x_raster, y_raster = x_canvas, y_canvas

                            try:
                                col, row = inverse_transform * (x_raster, y_raster)
                            except Exception:
                                # If transform fails, skip this corner
                                continue
                            raster_corners.append((col, row))

                        if len(raster_corners) < 4:
                            log_warning("RasterLayerRenderer: Failed to transform all corners")
                            return

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

                        # 4. Calculate scale factor for downsampling
                        if src_crs and dest_crs and src_crs != dest_crs:
                            # Reprojection case: use scale_factor=1 for accuracy
                            scale_factor = 1
                        else:
                            # Same CRS: can optimize by downsampling
                            screen_res = view_extent.width() / W if W > 0 else 1.0
                            raster_res = src.res[0] if src.res else 1.0
                            ratio = screen_res / raster_res
                            scale_factor = max(1, int(ratio))

                        # Solve analytically for the 6 affine warping coefficients
                        col_0, row_0 = raster_corners[0] # (0, 0)
                        col_W, row_W = raster_corners[1] # (W, 0)
                        col_H, row_H = raster_corners[3] # (0, H)

                        a_0 = (col_0 - col_off) / scale_factor
                        a_1 = (col_W - col_0) / (W * scale_factor) if W > 0 else 0
                        a_2 = (col_H - col_0) / (H * scale_factor) if H > 0 else 0

                        b_0 = (row_0 - row_off) / scale_factor
                        b_1 = (row_W - row_0) / (W * scale_factor) if W > 0 else 0
                        b_2 = (row_H - row_0) / (H * scale_factor) if H > 0 else 0

                        coeffs_x = np.array([a_0, a_1, a_2], dtype=np.float64)
                        coeffs_y = np.array([b_0, b_1, b_2], dtype=np.float64)

            except Exception as e:
                log_error(f"RasterLayerRenderer: GDAL/rasterio error: {e}")
                import traceback
                log_error(traceback.format_exc())
                return

            # 5. Read the cropped styled image from the decoupled strategy renderer
            try:
                # reader.read_raster_band uses geospatial_lock internally
                image_to_draw = self.renderer.render(
                    reader, scale_factor, window=window,
                    out_size=(W, H), coeffs_x=coeffs_x, coeffs_y=coeffs_y
                )
                if image_to_draw and not image_to_draw.isNull():
                    painter.save()
                    if self.opacity < 1.0:
                        painter.setOpacity(self.opacity)

                    # The warped image is already perfectly aligned with the viewport/device grid!
                    painter.drawImage(QRectF(0, 0, W, H), image_to_draw)
                    painter.restore()
            except Exception as e:
                log_error(f"RasterLayerRenderer: Error during image draw: {e}")
                import traceback
                log_error(traceback.format_exc())

        except Exception as e:
            log_error(f"RasterLayerRenderer: Unhandled error: {e}")
            import traceback
            log_error(traceback.format_exc())

RasterLayerRenderer = QgsRasterLayerRenderer

from PySide6.QtGui import QImage
from PySide6.QtCore import QRectF, Qt
import numpy as np
import rasterio
import rasterio.windows

from core.qgsmaplayerrenderer import QgsMapLayerRenderer
from core.logger import log_error, log_warning, log_debug, gdal_lock


class _PreReadBandReader:
    """Drop-in replacement for GeospatialReader that returns pre-read band data.

    Used by QgsRasterLayerRenderer.render() in a background thread where
    calling rasterio.open() would trigger a PROJ DatabaseContext SIGSEGV.
    All band data was pre-read on the main thread; this wrapper simply
    returns it without touching GDAL/PROJ.
    """
    def __init__(self, metadata, band_data):
        self.metadata = metadata
        self.is_raster = True
        self.file_path = metadata.get("_file_path", "")
        self._band_data = band_data  # {(band_idx, scale, window_key): ndarray}

    def read_raster_band(self, band_index, scale_factor=1, window=None):
        window_key = (window.col_off, window.row_off, window.width, window.height) if window else None
        key = (band_index, scale_factor, window_key)
        return self._band_data.get(key)


class QgsRasterLayerRenderer(QgsMapLayerRenderer):
    """
    Decoupled drawing class for Raster Layers matching QgsRasterLayerRenderer.

    All rasterio/GDAL/PROJ I/O happens on the main thread in __init__()
    (pre-reading band data). The render() method only composites and paints
    using pre-read numpy arrays — safe to call from a background thread.
    """
    def __init__(self, layer, settings):
        super().__init__(layer.id)
        self.crs = layer.crs
        self.extent = layer.extent
        self.opacity = layer.opacity
        self.visible = layer.visible
        self.file_path = layer.provider.reader.file_path
        self.reader = layer.provider.reader

        # Get the renderer from the pipe
        pipe = getattr(layer, '_pipe', None)
        if pipe is not None:
            self.renderer = pipe.renderer()
        else:
            self.renderer = None

        # Pre-create coordinate transform on MAIN thread using cache
        self._cached_transform = None
        dest_crs = settings.destination_crs if settings else None
        if self.crs and dest_crs and self.crs != dest_crs:
            from core.qgstransformcache import transform_cache
            self._cached_transform = transform_cache().get_transform(dest_crs, self.crs)

        # --- Pre-compute viewport & pre-read band data on MAIN thread ---
        self._pre_read_reader = None
        self._window = None
        self._scale_factor = 1
        self._coeffs_x = None
        self._coeffs_y = None
        self._W = 0
        self._H = 0

        if self.renderer and self.visible and settings:
            self._prepare_render_data(settings)

    def _prepare_render_data(self, settings):
        """Compute viewport window and pre-read all band data on the main thread.

        This is the ONLY method that calls rasterio.open() / PROJ — safe
        because it runs on the main thread before the job is dispatched
        to the QThreadPool.
        """
        try:
            reader = self.reader
            view_extent = settings.extent if settings.extent else self.extent
            dest_crs = settings.destination_crs

            src_crs = reader.metadata["crs"]
            src_width = reader.metadata["width"]
            src_height = reader.metadata["height"]

            W = settings.output_size.width() if settings.output_size else 1024
            H = settings.output_size.height() if settings.output_size else 768
            self._W = W
            self._H = H

            dx_c = view_extent.width() / W if W > 0 else 1.0
            dy_c = view_extent.height() / H if H > 0 else 1.0

            screen_corners = [(0, 0), (W, 0), (W, H), (0, H)]
            raster_corners = []
            transformer = self._cached_transform

            try:
                transform_meta = reader.metadata.get("transform")
                if transform_meta and len(transform_meta) >= 6:
                    inverse_transform = ~rasterio.Affine(*transform_meta[:6])
                else:
                    inverse_transform = ~rasterio.Affine.identity()
            except Exception as e:
                log_error(f"RasterLayerRenderer: Failed to compute inverse transform: {e}")
                return

            for u, v in screen_corners:
                x_canvas = view_extent.left() + u * dx_c
                y_canvas = view_extent.top() - v * dy_c

                if transformer:
                    try:
                        x_raster, y_raster = transformer.transform_xy(x_canvas, y_canvas)
                    except Exception:
                        x_raster, y_raster = x_canvas, y_canvas
                else:
                    x_raster, y_raster = x_canvas, y_canvas

                try:
                    col, row = inverse_transform * (x_raster, y_raster)
                except Exception:
                    continue
                raster_corners.append((col, row))

            if len(raster_corners) < 4:
                return

            cols = [pt[0] for pt in raster_corners]
            rows = [pt[1] for pt in raster_corners]
            col_min, col_max = min(cols), max(cols)
            row_min, row_max = min(rows), max(rows)

            if col_max < 0 or col_min >= src_width or row_max < 0 or row_min >= src_height:
                return

            col_off = max(0, min(src_width - 1, int(np.floor(col_min))))
            row_off = max(0, min(src_height - 1, int(np.floor(row_min))))
            col_max_clp = max(0, min(src_width - 1, int(np.ceil(col_max))))
            row_max_clp = max(0, min(src_height - 1, int(np.ceil(row_max))))
            width = max(1, col_max_clp - col_off + 1)
            height = max(1, row_max_clp - row_off + 1)

            window = rasterio.windows.Window(col_off, row_off, width, height)
            if window.width <= 0 or window.height <= 0:
                return

            self._window = window

            # Scale factor
            if src_crs and dest_crs and src_crs != dest_crs:
                scale_factor = 1
            else:
                screen_res = view_extent.width() / W if W > 0 else 1.0
                raster_res = abs(reader.metadata["transform"][0])
                ratio = screen_res / raster_res
                scale_factor = max(1, int(ratio))
            self._scale_factor = scale_factor

            # Affine warping coefficients
            col_0, row_0 = raster_corners[0]
            col_W, row_W = raster_corners[1]
            col_H, row_H = raster_corners[3]

            a_0 = (col_0 - col_off) / scale_factor
            a_1 = (col_W - col_0) / (W * scale_factor) if W > 0 else 0
            a_2 = (col_H - col_0) / (H * scale_factor) if H > 0 else 0
            b_0 = (row_0 - row_off) / scale_factor
            b_1 = (row_W - row_0) / (W * scale_factor) if W > 0 else 0
            b_2 = (row_H - row_0) / (H * scale_factor) if H > 0 else 0

            self._coeffs_x = np.array([a_0, a_1, a_2], dtype=np.float64)
            self._coeffs_y = np.array([b_0, b_1, b_2], dtype=np.float64)

            # --- Pre-read ALL band data on the main thread (safe for PROJ/GDAL) ---
            band_data = {}
            window_key = (window.col_off, window.row_off, window.width, window.height)

            # Determine which bands this renderer needs
            needed_bands = set()
            if hasattr(self.renderer, 'red_band'):
                needed_bands.add(self.renderer.red_band)
            if hasattr(self.renderer, 'green_band'):
                needed_bands.add(self.renderer.green_band)
            if hasattr(self.renderer, 'blue_band'):
                needed_bands.add(self.renderer.blue_band)
            if hasattr(self.renderer, 'gray_band'):
                needed_bands.add(self.renderer.gray_band)
            if hasattr(self.renderer, 'pseudocolor_band'):
                needed_bands.add(self.renderer.pseudocolor_band)

            for band_idx in needed_bands:
                data = reader.read_raster_band(band_idx, scale_factor, window=window)
                if data is not None:
                    band_data[(band_idx, scale_factor, window_key)] = data

            # Build pre-read reader wrapper (no rasterio/PROJ calls)
            metadata_copy = dict(reader.metadata)
            metadata_copy["_file_path"] = reader.file_path
            self._pre_read_reader = _PreReadBandReader(metadata_copy, band_data)

        except Exception as e:
            log_error(f"RasterLayerRenderer: Prepare error: {e}")
            import traceback
            log_error(traceback.format_exc())

    def render(self, painter, settings, renderContext=None):
        """Paint the pre-rendered image. Safe to call from a background thread.

        No rasterio/GDAL/PROJ calls happen here — all data was pre-read
        on the main thread by _prepare_render_data().
        """
        if not self.visible or painter is None:
            return

        if self.renderer is None:
            return

        if self._pre_read_reader is None:
            return

        try:
            image_to_draw = self.renderer.render(
                self._pre_read_reader, self._scale_factor, window=self._window,
                out_size=(self._W, self._H),
                coeffs_x=self._coeffs_x, coeffs_y=self._coeffs_y
            )
            if image_to_draw and not image_to_draw.isNull():
                painter.save()
                if self.opacity < 1.0:
                    painter.setOpacity(self.opacity)
                painter.drawImage(QRectF(0, 0, self._W, self._H), image_to_draw)
                painter.restore()
        except Exception as e:
            log_error(f"RasterLayerRenderer: Error during image draw: {e}")
            import traceback
            log_error(traceback.format_exc())


RasterLayerRenderer = QgsRasterLayerRenderer

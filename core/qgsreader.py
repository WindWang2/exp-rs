import os
import threading
import numpy as np
import rasterio
import fiona
from shapely.geometry import shape
from collections import OrderedDict
import time
from core.logger import log_info, log_debug, log_warning, log_error, gdal_lock

# Set global high-performance GDAL environment variables
os.environ["GDAL_CACHEMAX"] = "512"  # 512 MB block cache
os.environ["GDAL_DISABLE_READDIR_ON_OPEN"] = "EMPTY_DIR"  # Don't scan directory on open
os.environ["VSI_CACHE"] = "YES"  # Enable virtual file system cache
os.environ["VSI_CACHE_SIZE"] = "67108864"  # 64 MB virtual file system cache
os.environ["GDAL_HTTP_MERGE_CONSECUTIVE_RANGES"] = "YES"  # Highly efficient for COGs

class GeospatialReader:
    """
    Thread-safe geospatial reader wrapping rasterio and fiona.
    Enforces thread-isolated file handles to prevent GDAL concurrent segfaults.
    Utilizes an LRU Tiles Cache in RAM to accelerate repeated canvas zoom-pans.
    """
    def __init__(self, file_path: str):
        self.file_path = os.path.abspath(file_path)
        if not os.path.exists(self.file_path):
            log_error(f"GeospatialReader failed: File not found: {self.file_path}")
            raise FileNotFoundError(f"File not found: {self.file_path}")
        
        # Detect file type
        self.is_raster = self._detect_if_raster()
        self.metadata = self._read_initial_metadata()
        log_info(f"GeospatialReader opened file: '{os.path.basename(self.file_path)}' (is_raster={self.is_raster})")
        
        # Thread-local storage for thread-isolated tile caches
        self._thread_local = threading.local()

    def _detect_if_raster(self) -> bool:
        ext = os.path.splitext(self.file_path)[1].lower()
        if ext in ['.tif', '.tiff', '.img', '.dem', '.dat']:
            return True
        elif ext in ['.shp', '.geojson', '.gpkg', '.kml']:
            return False
        # Fallback probe
        try:
            with gdal_lock:
                with rasterio.open(self.file_path, sharing=False) as src:
                    return True
        except Exception:
            return False

    def _read_initial_metadata(self) -> dict:
        """Reads lightweight metadata on initialization."""
        if self.is_raster:
            with gdal_lock:
                with rasterio.open(self.file_path, sharing=False) as src:
                    bounds_dict = {"left": src.bounds.left, "bottom": src.bounds.bottom, "right": src.bounds.right, "top": src.bounds.top}
                    
                    overviews_list = []
                    for i in range(1, src.count + 1):
                        try:
                            overviews_list.append(src.overviews(i))
                        except Exception:
                            overviews_list.append([])

                    return {
                        "type": "raster",
                        "width": src.width,
                        "height": src.height,
                        "count": src.count,
                        "crs": src.crs.to_string() if src.crs else None,
                        "bounds": bounds_dict,
                        "transform": list(src.transform),
                        "dtypes": [str(t) for t in src.dtypes],
                        "overviews": overviews_list
                    }
        else:
            with gdal_lock:
                with fiona.open(self.file_path) as src:
                    b = src.bounds
                    bounds_dict = {"left": b[0], "bottom": b[1], "right": b[2], "top": b[3]} if b else {"left": 0.0, "bottom": 0.0, "right": 0.0, "top": 0.0}
                    return {
                        "type": "vector",
                        "driver": src.driver,
                        "crs": src.crs.get("init") if src.crs and isinstance(src.crs, dict) else (src.crs.to_string() if src.crs else None),
                        "bounds": bounds_dict,
                        "schema": dict(src.schema) if src.schema else {},
                        "count": len(src)
                    }

    def _find_best_overview(self, src, band_index: int, scale_factor: int) -> tuple:
        """Find the best matching GDAL overview for the given scale factor.
        Returns (overview_level, overview_scale) or (None, None) if no suitable overview."""
        try:
            overview_list = src.overviews(band_index)
        except Exception:
            return None, None

        if not overview_list:
            return None, None

        # Find the overview whose reduction factor is closest to (but not exceeding) scale_factor
        best_level = None
        best_scale = 1
        for level in overview_list:
            if level <= scale_factor and (best_level is None or level > best_scale):
                best_level = level
                best_scale = level

        return best_level, best_scale

    def read_raster_band(self, band_index: int, scale_factor: int = 1, window=None) -> np.ndarray:
        """
        Reads a single raster band, optionally cropped to a pixel window. Thread-isolated.
        Uses GDAL built-in overviews when available for fast large-image rendering.
        Utilizes an LRU Tiles Cache in RAM to prevent repeated disk reads.
        """
        if not self.is_raster:
            raise ValueError("File is not a raster dataset")

        # Get or initialize the thread-local tile cache
        if not hasattr(self._thread_local, "tile_cache"):
            self._thread_local.tile_cache = OrderedDict()
        tile_cache = self._thread_local.tile_cache
        cache_max_size = 32

        # Serialize window to a cacheable key
        window_tuple = (window.col_off, window.row_off, window.width, window.height) if window else None
        cache_key = (band_index, scale_factor, window_tuple)
        if cache_key in tile_cache:
            tile_cache.move_to_end(cache_key)
            return tile_cache[cache_key]

        with gdal_lock, rasterio.open(self.file_path, sharing=False) as src:
            if band_index < 1 or band_index > src.count:
                raise IndexError(f"Band index {band_index} out of range (1-{src.count})")

            # Try to use a GDAL built-in overview for better performance
            overview_level, overview_scale = (None, None)
            if scale_factor > 1:
                overview_level, overview_scale = self._find_best_overview(src, band_index, scale_factor)

            if overview_level is not None and overview_scale > 1:
                # Read from overview: adjust window and remaining scale
                ov_factor = scale_factor / overview_scale
                if window:
                    ov_col_off = int(window.col_off / overview_scale)
                    ov_row_off = int(window.row_off / overview_scale)
                    ov_width = max(1, int(window.width / overview_scale))
                    ov_height = max(1, int(window.height / overview_scale))
                    ov_window = rasterio.windows.Window(ov_col_off, ov_row_off, ov_width, ov_height)

                    if ov_factor > 1.5:
                        out_h = max(1, int(ov_height / ov_factor))
                        out_w = max(1, int(ov_width / ov_factor))
                        data = src.read(
                            band_index,
                            window=ov_window,
                            out_shape=(out_h, out_w),
                            resampling=rasterio.enums.Resampling.bilinear,
                            overview_level=overview_level
                        )
                    else:
                        data = src.read(band_index, window=ov_window, overview_level=overview_level)
                else:
                    # No window — read full overview, optionally further downsample
                    if ov_factor > 1.5:
                        # Let rasterio handle the additional downsampling from overview
                        out_h = max(1, int(src.height // overview_level // ov_factor))
                        out_w = max(1, int(src.width // overview_level // ov_factor))
                        data = src.read(
                            band_index,
                            out_shape=(out_h, out_w),
                            resampling=rasterio.enums.Resampling.bilinear,
                            overview_level=overview_level
                        )
                    else:
                        data = src.read(band_index, overview_level=overview_level)
            else:
                # No suitable overview — fall back to realtime downsample
                if window:
                    w_h = int(window.height)
                    w_w = int(window.width)
                    if scale_factor <= 1:
                        data = src.read(band_index, window=window)
                    else:
                        out_height = max(1, int(w_h / scale_factor))
                        out_width = max(1, int(w_w / scale_factor))
                        data = src.read(
                            band_index,
                            window=window,
                            out_shape=(out_height, out_width),
                            resampling=rasterio.enums.Resampling.bilinear
                        )
                else:
                    if scale_factor <= 1:
                        data = src.read(band_index)
                    else:
                        out_height = max(1, int(src.height / scale_factor))
                        out_width = max(1, int(src.width / scale_factor))
                        data = src.read(
                            band_index,
                            out_shape=(out_height, out_width),
                            resampling=rasterio.enums.Resampling.bilinear
                        )

        # Store in LRU Cache and pop oldest if size exceeded
        tile_cache[cache_key] = data
        if len(tile_cache) > cache_max_size:
            tile_cache.popitem(last=False)

        return data

    def read_vector_features(self) -> list:
        """
        Reads all vector features and their geometry shapes. Thread-isolated.
        """
        if self.is_raster:
            raise ValueError("File is not a vector dataset")
        
        features = []
        with gdal_lock, fiona.open(self.file_path) as src:
            for feat in src:
                geom = feat.get("geometry")
                geom_shape = shape(geom) if geom else None
                features.append({
                    "id": feat.get("id"),
                    "properties": dict(feat.get("properties", {})),
                    "geometry": geom,
                    "shape": geom_shape
                })
        return features

    def close(self):
        pass

    def __del__(self):
        pass

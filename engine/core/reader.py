import os
import numpy as np
import rasterio
import fiona
from shapely.geometry import shape
from collections import OrderedDict

class GeospatialReader:
    """
    Thread-safe geospatial reader wrapping rasterio and fiona.
    Enforces thread-isolated file handles to prevent GDAL concurrent segfaults.
    Utilizes an LRU Tiles Cache in RAM to accelerate repeated canvas zoom-pans.
    """
    def __init__(self, file_path: str):
        self.file_path = os.path.abspath(file_path)
        if not os.path.exists(self.file_path):
            raise FileNotFoundError(f"File not found: {self.file_path}")
        
        # Detect file type
        self.is_raster = self._detect_if_raster()
        self.metadata = self._read_initial_metadata()
        
        # Initialize thread-safe LRU Cache (limited to 32 downsampled bands in RAM)
        self._tile_cache = OrderedDict()
        self._cache_max_size = 32

    def _detect_if_raster(self) -> bool:
        ext = os.path.splitext(self.file_path)[1].lower()
        if ext in ['.tif', '.tiff', '.img', '.dem']:
            return True
        elif ext in ['.shp', '.geojson', '.gpkg', '.kml']:
            return False
        # Fallback probe
        try:
            with rasterio.open(self.file_path) as src:
                return True
        except Exception:
            return False

    def _read_initial_metadata(self) -> dict:
        """Reads lightweight metadata on initialization."""
        if self.is_raster:
            with rasterio.open(self.file_path) as src:
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

    def read_raster_band(self, band_index: int, scale_factor: int = 1) -> np.ndarray:
        """
        Reads a single raster band. Thread-isolated.
        Utilizes an LRU tiles cache in RAM to prevent repeated disk reads.
        """
        if not self.is_raster:
            raise ValueError("File is not a raster dataset")
            
        cache_key = (band_index, scale_factor)
        if cache_key in self._tile_cache:
            # Move key to end to mark as most recently used
            self._tile_cache.move_to_end(cache_key)
            return self._tile_cache[cache_key]
        
        # Open independent file handle in this thread and read
        with rasterio.open(self.file_path) as src:
            if band_index < 1 or band_index > src.count:
                raise IndexError(f"Band index {band_index} out of range (1-{src.count})")
            
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
        self._tile_cache[cache_key] = data
        if len(self._tile_cache) > self._cache_max_size:
            self._tile_cache.popitem(last=False)
            
        return data

    def read_vector_features(self) -> list:
        """
        Reads all vector features and their geometry shapes. Thread-isolated.
        """
        if self.is_raster:
            raise ValueError("File is not a vector dataset")
        
        features = []
        with fiona.open(self.file_path) as src:
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

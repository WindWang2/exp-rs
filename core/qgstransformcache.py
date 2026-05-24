"""QgsTransformCache - Thread-safe cache for coordinate transforms.

Pre-creates and caches QgsCoordinateReferenceSystem and QgsCoordinateTransform
objects on the main thread to avoid pyproj C extension segfaults when creating
them in background render threads.
"""
import threading
from typing import Dict, Tuple, Optional
import rasterio
import fiona
import pyproj
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem
from core.qgscoordinatetransform import QgsCoordinateTransform
from core.logger import geospatial_lock


class QgsTransformCache:
    """Thread-safe singleton cache for CRS and transform objects.

    The cache must be pre-warmed with common CRS on the main thread before
    any background rendering starts. This ensures pyproj's C extension
    objects are created in a thread-safe manner.
    """
    _instance = None
    _lock = threading.Lock()

    def __new__(cls):
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = super().__new__(cls)
                    cls._instance._initialized = False
        return cls._instance

    def __init__(self):
        if self._initialized:
            return
        self._initialized = True

        # Cache storage
        self._crs_cache: Dict[str, QgsCoordinateReferenceSystem] = {}
        self._transform_cache: Dict[Tuple[str, str], QgsCoordinateTransform] = {}
        self._cache_lock = threading.RLock()

        # Common CRS to pre-warm
        self._common_crs = [
            "EPSG:3857",  # Web Mercator
            "EPSG:4326",  # WGS84
            "EPSG:32650", # UTM Zone 50N
            "EPSG:32648", # UTM Zone 48N (Common in user tests)
        ]

    def warmup(self):
        """Pre-create common CRS objects on the main thread.

        Call this during app initialization to ensure pyproj C extension
        objects are created safely before background threads start.
        """
        # 1. Force GDAL environment initialization on main thread
        with geospatial_lock:
            with rasterio.Env():
                pass
            
        # 2. Pre-create common CRS within global lock
        with self._cache_lock:
            for authid in self._common_crs:
                if authid not in self._crs_cache:
                    try:
                        crs = QgsCoordinateReferenceSystem(authid)
                        if crs.isValid():
                            self._crs_cache[authid] = crs
                    except Exception:
                        pass

    def get_crs(self, authid: str) -> QgsCoordinateReferenceSystem:
        """Get or create a CRS object in a thread-safe manner."""
        with self._cache_lock:
            if authid in self._crs_cache:
                return self._crs_cache[authid]

            # Create new CRS (still within lock for thread safety)
            try:
                crs = QgsCoordinateReferenceSystem(authid)
                if crs.isValid():
                    self._crs_cache[authid] = crs
                    return crs
            except Exception:
                pass

            # Fallback: return invalid CRS
            return QgsCoordinateReferenceSystem()

    def get_transform(self, src_authid: str, dst_authid: str) -> Optional[QgsCoordinateTransform]:
        """Get or create a coordinate transform in a thread-safe manner.

        Returns None if the transform cannot be created.
        """
        # Short-circuit if same CRS
        if src_authid == dst_authid:
            return None

        key = (src_authid, dst_authid)
        with self._cache_lock:
            if key in self._transform_cache:
                cached = self._transform_cache[key]
                return cached if cached.isValid() else None

            # Create new transform within lock
            src_crs = self.get_crs(src_authid)
            dst_crs = self.get_crs(dst_authid)

            if not src_crs.isValid() or not dst_crs.isValid():
                return None

            try:
                transform = QgsCoordinateTransform(src_crs, dst_crs)
                if transform.isValid():
                    self._transform_cache[key] = transform
                    return transform
            except Exception:
                pass

            return None

    def register_layer_crs(self, authid: str):
        """Register a layer's CRS in the cache (call on main thread).

        This should be called when a layer is loaded to ensure its CRS
        is cached before any background rendering starts.
        """
        with self._cache_lock:
            if authid and authid not in self._crs_cache:
                try:
                    crs = QgsCoordinateReferenceSystem(authid)
                    if crs.isValid():
                        self._crs_cache[authid] = crs
                except Exception:
                    pass


# Global singleton instance
_transform_cache = QgsTransformCache()


def transform_cache() -> QgsTransformCache:
    """Return the global transform cache singleton."""
    return _transform_cache

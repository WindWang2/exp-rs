"""QgsCoordinateTransform - QGIS-compatible coordinate transform wrapper around pyproj."""
from typing import Tuple, List, Dict, Any
from pyproj import Transformer
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem
from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle
from core.logger import pyproj_lock


class QgsCoordinateTransform:
    """Handles coordinate reference system conversions for raster bounds,
    vector shapes, and canvas display coordinates.

    Accepts QgsCoordinateReferenceSystem objects (matching QGIS C++ API).

    Thread-safe: Uses a lock around pyproj Transformer operations.
    """
    __slots__ = ('_source_crs', '_dest_crs', '_transformer', '_inverse_transformer', '_short_circuited')

    def __init__(self, source_crs=None, dest_crs=None, context=None):
        # Accept both QgsCoordinateReferenceSystem and plain strings
        if isinstance(source_crs, str):
            source_crs = QgsCoordinateReferenceSystem(source_crs)
        if isinstance(dest_crs, str):
            dest_crs = QgsCoordinateReferenceSystem(dest_crs)
        self._source_crs = source_crs
        self._dest_crs = dest_crs
        self._transformer = None
        self._inverse_transformer = None
        self._short_circuited = False

        if source_crs is None or dest_crs is None:
            return

        if not source_crs.isValid() or not dest_crs.isValid():
            return

        src_auth = source_crs.authid()
        dst_auth = dest_crs.authid()
        if src_auth == dst_auth:
            self._short_circuited = True
            return

        # Use lock when creating pyproj Transformer (thread safety)
        with pyproj_lock:
            try:
                self._transformer = Transformer.from_crs(
                    source_crs.pyproj_crs(), dest_crs.pyproj_crs(), always_xy=True)
                self._inverse_transformer = Transformer.from_crs(
                    dest_crs.pyproj_crs(), source_crs.pyproj_crs(), always_xy=True)
            except Exception:
                self._transformer = None
                self._inverse_transformer = None

    # --- QGIS API surface ---

    def isValid(self) -> bool:
        return self._short_circuited or self._transformer is not None

    def isShortCircuited(self) -> bool:
        return self._short_circuited

    def sourceCrs(self) -> QgsCoordinateReferenceSystem:
        return self._source_crs

    def destinationCrs(self) -> QgsCoordinateReferenceSystem:
        return self._dest_crs

    def transform(self, point: QgsPointXY) -> QgsPointXY:
        if self._short_circuited:
            return point
        if self._transformer is None:
            return point
        with pyproj_lock:  # Thread-safe transform
            x, y = self._transformer.transform(point.x(), point.y())
        return QgsPointXY(x, y)

    def transform_xy(self, x: float, y: float) -> Tuple[float, float]:
        """Transform raw x,y coordinates. Returns (x', y')."""
        if self._short_circuited:
            return x, y
        if self._transformer is None:
            return x, y
        with pyproj_lock:  # Thread-safe transform
            return self._transformer.transform(x, y)

    def transformInPlace(self, x: float, y: float) -> Tuple[float, float]:
        return self.transform_xy(x, y)

    def transformRect(self, rect: QgsRectangle) -> QgsRectangle:
        if self._short_circuited:
            return rect
        if self._transformer is None:
            return rect
        with pyproj_lock:  # Thread-safe transform
            xs, ys = self._transformer.transform(
                [rect.xMinimum(), rect.xMinimum(), rect.xMaximum(), rect.xMaximum()],
                [rect.yMinimum(), rect.yMaximum(), rect.yMinimum(), rect.yMaximum()]
            )
        return QgsRectangle(min(xs), min(ys), max(xs), max(ys))

    def transformPolygon(self, polygon):
        if self._short_circuited:
            return polygon
        if self._transformer is None:
            return polygon
        from PySide6.QtGui import QPolygonF
        from PySide6.QtCore import QPointF
        result = QPolygonF()
        with pyproj_lock:  # Thread-safe transform
            for p in polygon:
                x, y = self._transformer.transform(p.x(), p.y())
                result.append(QPointF(x, y))
        return result

    def transform_geometry(self, geom_shape):
        if self._short_circuited:
            return geom_shape
        if self._transformer is None or geom_shape is None:
            return geom_shape
        from shapely.ops import transform as shapely_transform
        # shapely_transform calls the transformer function, so we need to wrap it
        def safe_transform(x, y):
            with pyproj_lock:
                return self._transformer.transform(x, y)
        return shapely_transform(safe_transform, geom_shape)

    def inverseTransform(self, point: QgsPointXY) -> QgsPointXY:
        if self._short_circuited:
            return point
        if self._inverse_transformer is None:
            return point
        with pyproj_lock:  # Thread-safe inverse transform
            x, y = self._inverse_transformer.transform(point.x(), point.y())
        return QgsPointXY(x, y)

    # --- Legacy API compatibility (used by existing code) ---

    def transform_bounds(self, left: float, bottom: float, right: float, top: float) -> Tuple[float, float, float, float]:
        """Transform a bounding box."""
        if self._short_circuited:
            return left, bottom, right, top
        if self._transformer is None:
            return left, bottom, right, top
        with pyproj_lock:  # Thread-safe transform
            xs, ys = self._transformer.transform(
                [left, left, right, right],
                [bottom, top, bottom, top]
            )
        return min(xs), min(ys), max(xs), max(ys)

    def transform_point(self, x: float, y: float) -> Tuple[float, float]:
        """Transform a single coordinate point (x, y) -> (x', y')."""
        return self.transform_xy(x, y)

    def inverse_transform_point(self, x: float, y: float) -> Tuple[float, float]:
        """Inverse transform a single coordinate point."""
        if self._short_circuited:
            return x, y
        if self._inverse_transformer is None:
            return x, y
        with pyproj_lock:  # Thread-safe inverse transform
            return self._inverse_transformer.transform(x, y)

    def inverse_transform_bounds(self, left: float, bottom: float, right: float, top: float) -> Tuple[float, float, float, float]:
        """Inverse transform a bounding box."""
        if self._short_circuited:
            return left, bottom, right, top
        if self._inverse_transformer is None:
            return left, bottom, right, top
        with pyproj_lock:  # Thread-safe inverse transform
            xs, ys = self._inverse_transformer.transform(
                [left, left, right, right],
                [bottom, top, bottom, top]
            )
        return min(xs), min(ys), max(xs), max(ys)


# Legacy alias
CRSTransformer = QgsCoordinateTransform

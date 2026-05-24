"""QgsCoordinateReferenceSystem - QGIS-compatible CRS wrapper around pyproj."""
from PySide6.QtCore import QObject, Signal
from pyproj import CRS
from core.logger import pyproj_lock


class QgsCoordinateReferenceSystem(QObject):
    """Wraps pyproj.CRS to provide the QGIS API surface (authid, fromEpsg, isGeographic, etc.).

    Inherits QObject for signal support (crsChanged).

    Thread-safe: Uses a lock around pyproj CRS operations.
    """

    crsChanged = Signal()

    def __init__(self, srs: str = ""):
        super().__init__()
        if srs:
            with pyproj_lock:  # Thread-safe CRS creation
                try:
                    self._crs = CRS.from_user_input(srs)
                except Exception:
                    self._crs = None
        else:
            self._crs = None

    def isValid(self) -> bool:
        return self._crs is not None

    def authid(self) -> str:
        if self._crs is None:
            return ""
        with pyproj_lock:  # Thread-safe access
            code = self._crs.to_authority()
        if code:
            return f"{code[0]}:{code[1]}"
        return ""

    def description(self) -> str:
        if self._crs is None:
            return ""
        with pyproj_lock:  # Thread-safe access
            return self._crs.name

    def toWkt(self) -> str:
        if self._crs is None:
            return ""
        with pyproj_lock:  # Thread-safe access
            return self._crs.to_wkt()

    def toProj(self) -> str:
        if self._crs is None:
            return ""
        with pyproj_lock:  # Thread-safe access
            return self._crs.to_proj4()

    def isGeographic(self) -> bool:
        if self._crs is None:
            return False
        with pyproj_lock:  # Thread-safe access
            return self._crs.is_geographic

    def mapUnits(self):
        from core.qgis import Qgis
        if self._crs is None:
            return Qgis.DistanceUnit.UnknownUnit
        with pyproj_lock:  # Thread-safe access
            is_geo = self._crs.is_geographic
        if is_geo:
            return Qgis.DistanceUnit.Degrees
        return Qgis.DistanceUnit.Meters

    def pyproj_crs(self) -> CRS:
        return self._crs

    @staticmethod
    def fromEpsg(epsg_id: int) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        with pyproj_lock:  # Thread-safe CRS creation
            try:
                crs._crs = CRS.from_epsg(epsg_id)
            except Exception:
                crs._crs = None
        return crs

    @staticmethod
    def fromWkt(wkt: str) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        with pyproj_lock:  # Thread-safe CRS creation
            try:
                crs._crs = CRS.from_wkt(wkt)
            except Exception:
                crs._crs = None
        return crs

    @staticmethod
    def fromProj(proj: str) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        with pyproj_lock:  # Thread-safe CRS creation
            try:
                crs._crs = CRS.from_proj4(proj)
            except Exception:
                crs._crs = None
        return crs

    @staticmethod
    def fromOgcWmsCrs(ogc: str) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        with pyproj_lock:  # Thread-safe CRS creation
            try:
                crs._crs = CRS.from_user_input(ogc)
            except Exception:
                crs._crs = None
        return crs

    @staticmethod
    def fromSrid(srid: int) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        with pyproj_lock:  # Thread-safe CRS creation
            try:
                crs._crs = CRS.from_user_input(f"urn:ogc:def:crs:EPSG::{srid}")
            except Exception:
                crs._crs = None
        return crs

    def __eq__(self, other):
        if not isinstance(other, QgsCoordinateReferenceSystem):
            return NotImplemented
        if self._crs is None and other._crs is None:
            return True
        if self._crs is None or other._crs is None:
            return False
        with pyproj_lock:  # Thread-safe comparison
            return self._crs == other._crs

    def __repr__(self):
        return f"QgsCoordinateReferenceSystem({self.authid()})"

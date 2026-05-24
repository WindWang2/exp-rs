import shapely
import shapely.wkt
import shapely.ops
from shapely.geometry import shape as shapely_shape, mapping
from shapely.geometry.base import BaseGeometry

from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle


class QgsGeometry:
    __slots__ = ('_geom',)

    def __init__(self, geom: BaseGeometry = None):
        self._geom = geom

    def isNull(self) -> bool:
        return self._geom is None

    def isEmpty(self) -> bool:
        if self._geom is None:
            return True
        return self._geom.is_empty

    def type(self):
        from core.qgis import Qgis
        if self._geom is None:
            return Qgis.GeometryType.Null
        gt = self._geom.geom_type
        if gt in ('Point', 'MultiPoint'):
            return Qgis.GeometryType.Point
        elif gt in ('LineString', 'MultiLineString', 'LinearRing'):
            return Qgis.GeometryType.Line
        elif gt in ('Polygon', 'MultiPolygon'):
            return Qgis.GeometryType.Polygon
        return Qgis.GeometryType.Unknown

    def area(self) -> float:
        if self._geom is None:
            return 0.0
        return self._geom.area

    def length(self) -> float:
        if self._geom is None:
            return 0.0
        return self._geom.length

    def centroid(self) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.centroid)

    def boundingBox(self) -> QgsRectangle:
        if self._geom is None:
            return QgsRectangle()
        b = self._geom.bounds  # (minx, miny, maxx, maxy)
        return QgsRectangle(b[0], b[1], b[2], b[3])

    def buffer(self, distance: float, segments: int = 8) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.buffer(distance, segments))

    def simplify(self, tolerance: float) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.simplify(tolerance))

    def makeValid(self) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        return QgsGeometry(shapely.validation.make_valid(self._geom))

    def contains(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.contains(other._geom)

    def intersects(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.intersects(other._geom)

    def disjoint(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return True
        return self._geom.disjoint(other._geom)

    def within(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.within(other._geom)

    def crosses(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.crosses(other._geom)

    def touches(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.touches(other._geom)

    def overlaps(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.overlaps(other._geom)

    def combine(self, other: 'QgsGeometry') -> 'QgsGeometry':
        if self._geom is None:
            return other
        if other._geom is None:
            return self
        return QgsGeometry(self._geom.union(other._geom))

    def difference(self, other: 'QgsGeometry') -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        if other._geom is None:
            return self
        return QgsGeometry(self._geom.difference(other._geom))

    def intersection(self, other: 'QgsGeometry') -> 'QgsGeometry':
        if self._geom is None or other._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.intersection(other._geom))

    def symDifference(self, other: 'QgsGeometry') -> 'QgsGeometry':
        if self._geom is None or other._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.symmetric_difference(other._geom))

    def transform(self, ct) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        from shapely.ops import transform as shapely_transform
        return QgsGeometry(shapely_transform(ct.transform, self._geom))

    def asWkt(self) -> str:
        if self._geom is None:
            return ""
        return self._geom.wkt

    def asWkb(self) -> bytes:
        if self._geom is None:
            return b""
        return shapely.to_wkb(self._geom)

    def asPoint(self) -> QgsPointXY:
        if self._geom is None:
            return QgsPointXY()
        return QgsPointXY(self._geom.x, self._geom.y)

    def asPolygon(self) -> list:
        if self._geom is None:
            return []
        if self._geom.geom_type == 'Polygon':
            return [[QgsPointXY(x, y) for x, y in self._geom.exterior.coords]]
        return []

    @staticmethod
    def fromWkt(wkt: str) -> 'QgsGeometry':
        try:
            return QgsGeometry(shapely.wkt.loads(wkt))
        except Exception:
            return QgsGeometry()

    @staticmethod
    def fromWkb(wkb: bytes) -> 'QgsGeometry':
        try:
            return QgsGeometry(shapely.from_wkb(wkb))
        except Exception:
            return QgsGeometry()

    @staticmethod
    def fromPointXY(point: QgsPointXY) -> 'QgsGeometry':
        from shapely.geometry import Point
        return QgsGeometry(Point(point.x(), point.y()))

    @staticmethod
    def fromPolyline(points: list) -> 'QgsGeometry':
        from shapely.geometry import LineString
        return QgsGeometry(LineString([(p.x(), p.y()) for p in points]))

    @staticmethod
    def fromPolygon(rings: list) -> 'QgsGeometry':
        from shapely.geometry import Polygon
        exterior = [(p.x(), p.y()) for p in rings[0]]
        holes = [[(p.x(), p.y()) for p in ring] for ring in rings[1:]] if len(rings) > 1 else []
        return QgsGeometry(Polygon(exterior, holes))

    @staticmethod
    def fromJson(geojson: dict) -> 'QgsGeometry':
        try:
            return QgsGeometry(shapely_shape(geojson))
        except Exception:
            return QgsGeometry()

    def __eq__(self, other):
        if not isinstance(other, QgsGeometry):
            return NotImplemented
        if self._geom is None and other._geom is None:
            return True
        if self._geom is None or other._geom is None:
            return False
        return self._geom.equals(other._geom)

    def __repr__(self):
        if self._geom is None:
            return "QgsGeometry(NULL)"
        return f"QgsGeometry({self._geom.geom_type})"

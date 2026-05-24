import enum

from core.qgis import Qgis


class QgsWkbTypes:
    class Type(enum.IntEnum):
        Unknown = 0
        Point = 1
        LineString = 2
        Polygon = 3
        Triangle = 17
        MultiPoint = 4
        MultiLineString = 5
        MultiPolygon = 6
        GeometryCollection = 7
        CircularString = 8
        CompoundCurve = 9
        CurvePolygon = 10
        MultiCurve = 11
        MultiSurface = 12
        NoGeometry = 100
        PointZ = 1001
        LineStringZ = 1002
        PolygonZ = 1003
        MultiPointZ = 1004
        MultiLineStringZ = 1005
        MultiPolygonZ = 1006
        PointM = 2001
        LineStringM = 2002
        PolygonM = 2003
        PointZM = 3001
        LineStringZM = 3002
        PolygonZM = 3003

    @staticmethod
    def flatType(wkb_type: int) -> int:
        return wkb_type % 1000

    @staticmethod
    def geometryType(wkb_type: int) -> Qgis.GeometryType:
        flat = QgsWkbTypes.flatType(wkb_type)
        if flat in (1, 4, 17):
            return Qgis.GeometryType.Point
        elif flat in (2, 5, 8, 9, 11):
            return Qgis.GeometryType.Line
        elif flat in (3, 6, 10, 12):
            return Qgis.GeometryType.Polygon
        elif flat == 7:
            return Qgis.GeometryType.Unknown
        elif flat == 100:
            return Qgis.GeometryType.Null
        return Qgis.GeometryType.Unknown

    @staticmethod
    def isMultiType(wkb_type: int) -> bool:
        flat = QgsWkbTypes.flatType(wkb_type)
        return flat in (4, 5, 6, 7, 11, 12)

    @staticmethod
    def hasZ(wkb_type: int) -> bool:
        return wkb_type >= 1000 and wkb_type < 2000

    @staticmethod
    def hasM(wkb_type: int) -> bool:
        return (wkb_type >= 2000 and wkb_type < 3000) or wkb_type >= 3000

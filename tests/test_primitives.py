from core.qgis import Qgis


def test_geometry_type_enum():
    assert Qgis.GeometryType.Point == 0
    assert Qgis.GeometryType.Line == 1
    assert Qgis.GeometryType.Polygon == 2


def test_layer_type_enum():
    assert Qgis.LayerType.Raster == 0
    assert Qgis.LayerType.Vector == 1


def test_data_type_enum():
    assert Qgis.DataType.Byte == 1
    assert Qgis.DataType.UInt16 == 2
    assert Qgis.DataType.Float32 == 6


def test_distance_unit_enum():
    assert Qgis.DistanceUnit.Meters == 0
    assert Qgis.DistanceUnit.Degrees == 1


def test_raster_layer_type_enum():
    assert Qgis.RasterLayerType.GrayOrUndefined == 0
    assert Qgis.RasterLayerType.Multiband == 1
    assert Qgis.RasterLayerType.Palette == 2


from core.qgspointxy import QgsPointXY
import math


def test_point_creation():
    p = QgsPointXY(1.0, 2.0)
    assert p.x() == 1.0
    assert p.y() == 2.0


def test_point_default():
    p = QgsPointXY()
    assert p.x() == 0.0
    assert p.y() == 0.0


def test_point_setters():
    p = QgsPointXY()
    p.setX(5.0)
    p.setY(10.0)
    assert p.x() == 5.0
    assert p.y() == 10.0


def test_point_distance():
    p1 = QgsPointXY(0, 0)
    p2 = QgsPointXY(3, 4)
    assert p1.distance(p2) == 5.0


def test_point_is_null():
    p = QgsPointXY()
    assert p.isEmpty()


def test_point_equality():
    p1 = QgsPointXY(1, 2)
    p2 = QgsPointXY(1, 2)
    assert p1 == p2


def test_point_repr():
    p = QgsPointXY(1.5, 2.5)
    assert "1.5" in repr(p)
    assert "2.5" in repr(p)


# --- QgsRectangle tests ---

from core.qgsrectangle import QgsRectangle


def test_rect_creation():
    r = QgsRectangle(0, 0, 10, 10)
    assert r.xMinimum() == 0.0
    assert r.yMinimum() == 0.0
    assert r.xMaximum() == 10.0
    assert r.yMaximum() == 10.0


def test_rect_dimensions():
    r = QgsRectangle(0, 0, 10, 5)
    assert r.width() == 10.0
    assert r.height() == 5.0
    assert r.area() == 50.0


def test_rect_center():
    r = QgsRectangle(0, 0, 10, 10)
    c = r.center()
    assert c.x() == 5.0
    assert c.y() == 5.0


def test_rect_contains():
    r = QgsRectangle(0, 0, 10, 10)
    assert r.contains(QgsPointXY(5, 5))
    assert not r.contains(QgsPointXY(15, 5))


def test_rect_intersects():
    r1 = QgsRectangle(0, 0, 10, 10)
    r2 = QgsRectangle(5, 5, 15, 15)
    r3 = QgsRectangle(20, 20, 30, 30)
    assert r1.intersects(r2)
    assert not r1.intersects(r3)


def test_rect_is_empty():
    r = QgsRectangle()
    assert r.isEmpty()


def test_rect_normalize():
    r = QgsRectangle(10, 10, 0, 0)
    r.normalize()
    assert r.xMinimum() == 0.0
    assert r.yMinimum() == 0.0


def test_rect_grow():
    r = QgsRectangle(2, 2, 8, 8)
    r.grow(2)
    assert r.xMinimum() == 0.0
    assert r.xMaximum() == 10.0


def test_rect_union():
    r1 = QgsRectangle(0, 0, 5, 5)
    r2 = QgsRectangle(3, 3, 10, 10)
    u = r1.united(r2)
    assert u.xMinimum() == 0.0
    assert u.xMaximum() == 10.0


def test_rect_from_point():
    r = QgsRectangle.fromCenterAndSize(QgsPointXY(5, 5), 10, 10)
    assert r.width() == 10.0
    assert r.height() == 10.0

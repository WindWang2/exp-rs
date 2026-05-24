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

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

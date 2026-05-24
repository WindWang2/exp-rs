from core.qgsgeometry import QgsGeometry
from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle


def test_geometry_from_point():
    g = QgsGeometry.fromPointXY(QgsPointXY(1, 2))
    assert not g.isNull()
    assert not g.isEmpty()


def test_geometry_from_wkt():
    g = QgsGeometry.fromWkt("POINT(1 2)")
    assert not g.isNull()


def test_geometry_area():
    g = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    assert g.area() == 100.0


def test_geometry_length():
    g = QgsGeometry.fromWkt("LINESTRING(0 0, 3 4)")
    assert g.length() == 5.0


def test_geometry_centroid():
    g = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    c = g.centroid()
    assert c.asPoint().x() == 5.0
    assert c.asPoint().y() == 5.0


def test_geometry_bbox():
    g = QgsGeometry.fromWkt("POINT(5 10)")
    bb = g.boundingBox()
    assert bb.xMinimum() == 5.0
    assert bb.yMaximum() == 10.0


def test_geometry_buffer():
    g = QgsGeometry.fromWkt("POINT(0 0)")
    b = g.buffer(1.0, 8)
    assert b.area() > 0


def test_geometry_contains():
    g1 = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    g2 = QgsGeometry.fromWkt("POINT(5 5)")
    assert g1.contains(g2)


def test_geometry_intersects():
    g1 = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    g2 = QgsGeometry.fromWkt("POLYGON((5 5, 15 5, 15 15, 5 15, 5 5))")
    assert g1.intersects(g2)


def test_geometry_combine():
    g1 = QgsGeometry.fromWkt("POLYGON((0 0, 5 0, 5 5, 0 5, 0 0))")
    g2 = QgsGeometry.fromWkt("POLYGON((5 0, 10 0, 10 5, 5 5, 5 0))")
    u = g1.combine(g2)
    assert u.area() == 50.0


def test_geometry_difference():
    g1 = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    g2 = QgsGeometry.fromWkt("POLYGON((5 5, 15 5, 15 15, 5 15, 5 5))")
    d = g1.difference(g2)
    assert d.area() < g1.area()


def test_geometry_as_wkt():
    g = QgsGeometry.fromWkt("POINT(1 2)")
    wkt = g.asWkt()
    assert "POINT" in wkt


def test_geometry_is_empty():
    g = QgsGeometry()
    assert g.isNull()

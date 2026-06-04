// Geometry tests — verify QgsGeometry operations
#include <catch2/catch_test_macros.hpp>

#include <qgsgeometry.h>
#include <qgspointxy.h>
#include <qgspoint.h>

TEST_CASE("Geometry from point", "[geometry]") {
  SECTION("Point geometry from QgsPointXY") {
    QgsPointXY pt(116.4, 39.9);
    QgsGeometry geom = QgsGeometry::fromPointXY(pt);
    REQUIRE_FALSE(geom.isEmpty());
    REQUIRE(geom.type() == Qgis::GeometryType::Point);
  }

  SECTION("Empty geometry") {
    QgsGeometry empty;
    REQUIRE(empty.isEmpty());
  }
}

TEST_CASE("Geometry from WKT", "[geometry]") {
  SECTION("Point WKT") {
    QgsGeometry geom = QgsGeometry::fromWkt("POINT(116.4 39.9)");
    REQUIRE_FALSE(geom.isEmpty());
    REQUIRE(geom.type() == Qgis::GeometryType::Point);
  }

  SECTION("LineString WKT") {
    QgsGeometry geom = QgsGeometry::fromWkt("LINESTRING(0 0, 1 1, 2 0)");
    REQUIRE_FALSE(geom.isEmpty());
    REQUIRE(geom.type() == Qgis::GeometryType::Line);
  }

  SECTION("Polygon WKT") {
    QgsGeometry geom = QgsGeometry::fromWkt("POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))");
    REQUIRE_FALSE(geom.isEmpty());
    REQUIRE(geom.type() == Qgis::GeometryType::Polygon);
  }
}

TEST_CASE("Geometry from polyline", "[geometry]") {
  QgsPolylineXY polyline = {QgsPointXY(0, 0), QgsPointXY(1, 1), QgsPointXY(2, 0)};
  QgsGeometry geom = QgsGeometry::fromPolylineXY(polyline);
  REQUIRE_FALSE(geom.isEmpty());
  REQUIRE(geom.type() == Qgis::GeometryType::Line);
}

TEST_CASE("Geometry from polygon", "[geometry]") {
  QgsPolygonXY polygon;
  QgsPolylineXY ring = {QgsPointXY(0, 0), QgsPointXY(1, 0), QgsPointXY(1, 1), QgsPointXY(0, 1), QgsPointXY(0, 0)};
  polygon.append(ring);
  QgsGeometry geom = QgsGeometry::fromPolygonXY(polygon);
  REQUIRE_FALSE(geom.isEmpty());
  REQUIRE(geom.type() == Qgis::GeometryType::Polygon);
}

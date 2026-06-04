// CRS tests — verify coordinate reference system operations
#include <catch2/catch_test_macros.hpp>

#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatetransformcontext.h>
#include <qgspointxy.h>

TEST_CASE("CRS from EPSG code", "[crs]") {
  SECTION("WGS84 (EPSG:4326)") {
    QgsCoordinateReferenceSystem crs("EPSG:4326");
    REQUIRE(crs.isValid());
    REQUIRE(crs.authid() == "EPSG:4326");
  }

  SECTION("Web Mercator (EPSG:3857)") {
    QgsCoordinateReferenceSystem crs("EPSG:3857");
    REQUIRE(crs.isValid());
    REQUIRE(crs.authid() == "EPSG:3857");
  }

  SECTION("UTM Zone 48N (EPSG:32648)") {
    QgsCoordinateReferenceSystem crs("EPSG:32648");
    REQUIRE(crs.isValid());
  }
}

TEST_CASE("CRS from PROJ string", "[crs]") {
  QgsCoordinateReferenceSystem crs;
  crs.createFromProj("+proj=longlat +datum=WGS84 +no_defs");
  REQUIRE(crs.isValid());
}

TEST_CASE("Coordinate transform", "[crs]") {
  QgsCoordinateReferenceSystem srcCrs("EPSG:4326");
  QgsCoordinateReferenceSystem dstCrs("EPSG:3857");
  REQUIRE(srcCrs.isValid());
  REQUIRE(dstCrs.isValid());

  QgsCoordinateTransformContext ctx;
  QgsCoordinateTransform xform(srcCrs, dstCrs, ctx);

  // Transform a point from WGS84 to Web Mercator
  QgsPointXY wgs84(116.4, 39.9); // Beijing
  QgsPointXY mercator = xform.transform(wgs84);

  // Web Mercator X should be ~12959000, Y should be ~4846000 for Beijing
  REQUIRE(mercator.x() > 12000000);
  REQUIRE(mercator.x() < 14000000);
  REQUIRE(mercator.y() > 4000000);
  REQUIRE(mercator.y() < 6000000);
}

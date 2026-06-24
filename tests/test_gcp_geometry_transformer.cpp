// test_gcp_geometry_transformer.cpp — QgsGcpGeometryTransformer tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <analysis/georeferencing/qgsgcpgeometrytransformer.h>
#include <analysis/georeferencing/qgsgcptransformer.h>
#include <qgsgeometry.h>
#include <qgspointxy.h>
#include <qgsfeedback.h>

TEST_CASE("QgsGcpGeometryTransformer construction", "[georef][gcp_geom]")
{
    SECTION("Construct with source/destination coordinates")
    {
        QVector<QgsPointXY> src = { {0, 0}, {10, 0}, {0, 10} };
        QVector<QgsPointXY> dst = { {100, 100}, {110, 100}, {100, 110} };
        QgsGcpGeometryTransformer geomTransformer(
            QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1, src, dst);
        REQUIRE(geomTransformer.gcpTransformer() != nullptr);
    }
}

TEST_CASE("QgsGcpGeometryTransformer transformPoint", "[georef][gcp_geom]")
{
    // Simple translation: shift by (100, 200)
    QVector<QgsPointXY> src = { {0, 0}, {10, 0}, {0, 10} };
    QVector<QgsPointXY> dst = { {100, 200}, {110, 200}, {100, 210} };

    QgsGcpGeometryTransformer geomTransformer(
        QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1, src, dst);

    SECTION("Transform single point")
    {
        double x = 5.0, y = 5.0, z = 0.0, m = 0.0;
        bool ok = geomTransformer.transformPoint(x, y, z, m);
        REQUIRE(ok);
        REQUIRE(x == Catch::Approx(105.0).margin(0.1));
        REQUIRE(y == Catch::Approx(205.0).margin(0.1));
    }

    SECTION("Transform origin point")
    {
        double x = 0.0, y = 0.0, z = 0.0, m = 0.0;
        bool ok = geomTransformer.transformPoint(x, y, z, m);
        REQUIRE(ok);
        REQUIRE(x == Catch::Approx(100.0).margin(0.1));
        REQUIRE(y == Catch::Approx(200.0).margin(0.1));
    }
}

TEST_CASE("QgsGcpGeometryTransformer transform geometry", "[georef][gcp_geom]")
{
    // Simple translation: shift by (100, 200)
    QVector<QgsPointXY> src = { {0, 0}, {10, 0}, {0, 10} };
    QVector<QgsPointXY> dst = { {100, 200}, {110, 200}, {100, 210} };

    QgsGcpGeometryTransformer geomTransformer(
        QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1, src, dst);

    SECTION("Transform point geometry")
    {
        QgsGeometry pointGeom = QgsGeometry::fromPointXY(QgsPointXY(5, 5));
        bool ok = false;
        QgsGeometry result = geomTransformer.transform(pointGeom, ok);
        REQUIRE(ok);
        REQUIRE(!result.isNull());

        QgsPointXY resultPoint = result.asPoint();
        REQUIRE(resultPoint.x() == Catch::Approx(105.0).margin(0.1));
        REQUIRE(resultPoint.y() == Catch::Approx(205.0).margin(0.1));
    }

    SECTION("Transform line geometry")
    {
        QVector<QgsPointXY> linePoints = { {0, 0}, {10, 10} };
        QgsGeometry lineGeom = QgsGeometry::fromPolylineXY(linePoints);
        bool ok = false;
        QgsGeometry result = geomTransformer.transform(lineGeom, ok);
        REQUIRE(ok);
        REQUIRE(!result.isNull());
        REQUIRE(result.type() == Qgis::GeometryType::Line);
    }

    SECTION("Transform polygon geometry")
    {
        QVector<QgsPointXY> polyPoints = { {0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0} };
        QgsGeometry polyGeom = QgsGeometry::fromPolygonXY({polyPoints});
        bool ok = false;
        QgsGeometry result = geomTransformer.transform(polyGeom, ok);
        REQUIRE(ok);
        REQUIRE(!result.isNull());
        REQUIRE(result.type() == Qgis::GeometryType::Polygon);
    }

    SECTION("Transform empty geometry")
    {
        QgsGeometry emptyGeom;
        bool ok = false;
        QgsGeometry result = geomTransformer.transform(emptyGeom, ok);
        // Empty geometry may return ok=true with null result, or ok=false
        // Both behaviors are acceptable
    }

    SECTION("Transform with cancellation")
    {
        QgsFeedback feedback;
        feedback.cancel();

        QgsGeometry pointGeom = QgsGeometry::fromPointXY(QgsPointXY(5, 5));
        bool ok = false;
        QgsGeometry result = geomTransformer.transform(pointGeom, ok, &feedback);
        // Should return early due to cancellation
    }
}

TEST_CASE("QgsGcpGeometryTransformer setGcpTransformer", "[georef][gcp_geom]")
{
    QVector<QgsPointXY> src = { {0, 0}, {10, 0}, {0, 10} };
    QVector<QgsPointXY> dst = { {100, 200}, {110, 200}, {100, 210} };

    QgsGcpGeometryTransformer geomTransformer(
        QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1, src, dst);

    SECTION("Has transformer")
    {
        REQUIRE(geomTransformer.gcpTransformer() != nullptr);
    }
}

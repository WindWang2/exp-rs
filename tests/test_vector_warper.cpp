// test_vector_warper.cpp — QgsVectorWarper tests
#include <catch2/catch_test_macros.hpp>

#include <analysis/georeferencing/qgsvectorwarper.h>
#include <analysis/georeferencing/qgsgcppoint.h>
#include <qgsgeometry.h>
#include <qgspointxy.h>
#include <qgsfeature.h>
#include <qgsfields.h>
#include <qgsfeaturerequest.h>
#include <qgsvectorlayer.h>
#include <qgsproject.h>
#include <qgsfeedback.h>

#include <QTemporaryFile>

TEST_CASE("QgsVectorWarper construction", "[georef][warper]")
{
    QgsCoordinateReferenceSystem crs("EPSG:4326");
    QList<QgsGcpPoint> points;
    points << QgsGcpPoint(QgsPointXY(0, 0), QgsPointXY(100, 200), crs)
           << QgsGcpPoint(QgsPointXY(10, 0), QgsPointXY(110, 200), crs)
           << QgsGcpPoint(QgsPointXY(0, 10), QgsPointXY(100, 210), crs);

    SECTION("Construct with Linear method")
    {
        QgsVectorWarper warper(QgsGcpTransformerInterface::TransformMethod::Linear,
                               points, crs);
        REQUIRE(warper.error().isEmpty());
    }

    SECTION("Construct with PolynomialOrder1 method")
    {
        QgsVectorWarper warper(QgsGcpTransformerInterface::TransformMethod::PolynomialOrder1,
                               points, crs);
        REQUIRE(warper.error().isEmpty());
    }
}

TEST_CASE("QgsVectorWarper transformFeatures", "[georef][warper]")
{
    QgsCoordinateReferenceSystem crs("EPSG:4326");

    // Create a simple point vector layer
    QgsVectorLayer *layer = new QgsVectorLayer("Point?crs=EPSG:4326", "test", "memory");
    REQUIRE(layer->isValid());

    QgsFeature f1;
    f1.setGeometry(QgsGeometry::fromPointXY(QgsPointXY(1, 1)));

    QgsFeature f2;
    f2.setGeometry(QgsGeometry::fromPointXY(QgsPointXY(5, 5)));

    layer->dataProvider()->addFeature(f1);
    layer->dataProvider()->addFeature(f2);

    // GCPs: simple translation by (100, 200)
    QList<QgsGcpPoint> points;
    points << QgsGcpPoint(QgsPointXY(0, 0), QgsPointXY(100, 200), crs)
           << QgsGcpPoint(QgsPointXY(10, 0), QgsPointXY(110, 200), crs)
           << QgsGcpPoint(QgsPointXY(0, 10), QgsPointXY(100, 210), crs);

    QgsVectorWarper warper(QgsGcpTransformerInterface::TransformMethod::Linear,
                           points, crs);

    SECTION("Transform with cancellation")
    {
        QgsVectorLayer *outLayer = new QgsVectorLayer("Point?crs=EPSG:4326", "output", "memory");
        QgsFeatureIterator iter = layer->getFeatures();
        QgsFeedback feedback;
        feedback.cancel();

        bool ok = warper.transformFeatures(iter, outLayer->dataProvider(),
                                            QgsProject::instance()->transformContext(), &feedback);
        // Should return false due to cancellation
        REQUIRE(!ok);
        delete outLayer;
    }

    delete layer;
}

TEST_CASE("QgsVectorWarperTask construction", "[georef][warper]")
{
    QgsCoordinateReferenceSystem crs("EPSG:4326");
    QList<QgsGcpPoint> points;
    points << QgsGcpPoint(QgsPointXY(0, 0), QgsPointXY(100, 200), crs)
           << QgsGcpPoint(QgsPointXY(10, 0), QgsPointXY(110, 200), crs)
           << QgsGcpPoint(QgsPointXY(0, 10), QgsPointXY(100, 210), crs);

    QgsVectorLayer *layer = new QgsVectorLayer("Point?crs=EPSG:4326", "test", "memory");
    QTemporaryFile tmpFile;
    tmpFile.open();

    SECTION("Task construction")
    {
        QgsVectorWarperTask task(QgsGcpTransformerInterface::TransformMethod::Linear,
                                 points, crs, layer, tmpFile.fileName());
        REQUIRE(task.result() == QgsVectorWarperTask::Result::Success);
        REQUIRE(task.errorMessage().isEmpty());
    }

    SECTION("Task cancel")
    {
        QgsVectorWarperTask task(QgsGcpTransformerInterface::TransformMethod::Linear,
                                 points, crs, layer, tmpFile.fileName());
        task.cancel();
        // After cancel, task should report canceled when run
    }

    delete layer;
}

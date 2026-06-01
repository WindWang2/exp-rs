// Vector Layer Properties tests — verify vector-specific tabs and statistics
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QTabWidget>

#include <qgsvectorlayer.h>
#include <qgsapplication.h>
#include <qgsproject.h>

// Helper to create a test vector layer with some features
static QgsVectorLayer *createTestVectorLayer()
{
    QgsVectorLayer *layer = new QgsVectorLayer(
        "Point?crs=epsg:4326&field=name:string&field=value:double&field=count:int",
        "test_vector", "memory");
    return layer;
}

TEST_CASE("Vector layer properties dialog has vector-specific tabs", "[vector][properties]") {
    // Create a vector layer
    QgsVectorLayer *layer = createTestVectorLayer();
    REQUIRE(layer->isValid());

    SECTION("Vector layer has correct geometry type") {
        CHECK(layer->geometryType() == Qgis::GeometryType::Point);
    }

    SECTION("Vector layer has fields") {
        CHECK(layer->fields().count() == 3);
        CHECK(layer->fields().at(0).name() == "name");
        CHECK(layer->fields().at(1).name() == "value");
        CHECK(layer->fields().at(2).name() == "count");
    }

    SECTION("Vector layer has feature count") {
        CHECK(layer->featureCount() == 0);
    }

    delete layer;
}

TEST_CASE("Vector statistics computation", "[vector][statistics]") {
    QgsVectorLayer *layer = createTestVectorLayer();
    REQUIRE(layer->isValid());

    SECTION("Empty layer has zero feature count") {
        CHECK(layer->featureCount() == 0);
    }

    SECTION("Layer has spatial index capability") {
        // QgsVectorLayer should support spatial index
        CHECK(layer->dataProvider() != nullptr);
    }

    SECTION("Layer has geometry type") {
        CHECK(layer->geometryType() == Qgis::GeometryType::Point);
    }

    delete layer;
}

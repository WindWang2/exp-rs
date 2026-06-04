// Layer tests — verify layer creation and metadata
#include <catch2/catch_test_macros.hpp>

#include <qgsvectorlayer.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>

TEST_CASE("Vector layer creation", "[layers]") {
  SECTION("Empty vector layer is invalid") {
    QgsVectorLayer layer("", "empty", "ogr");
    REQUIRE_FALSE(layer.isValid());
  }

  SECTION("Memory provider vector layer") {
    QgsVectorLayer layer("Point?crs=EPSG:4326", "points", "memory");
    REQUIRE(layer.isValid());
    REQUIRE(layer.name() == "points");
  }

  SECTION("Memory provider line layer") {
    QgsVectorLayer layer("LineString?crs=EPSG:4326", "lines", "memory");
    REQUIRE(layer.isValid());
    REQUIRE(layer.type() == Qgis::LayerType::Vector);
  }
}

TEST_CASE("Raster layer creation", "[layers]") {
  SECTION("Invalid raster returns invalid") {
    QgsRasterLayer layer("/nonexistent/file.tif", "raster");
    REQUIRE_FALSE(layer.isValid());
  }
}

TEST_CASE("Project layer management", "[layers]") {
  QgsProject project;

  SECTION("Add vector layer to project") {
    auto *layer = new QgsVectorLayer("Point?crs=EPSG:4326", "test_points", "memory");
    REQUIRE(layer->isValid());

    QgsMapLayer *added = project.addMapLayer(layer);
    REQUIRE(added != nullptr);
    REQUIRE(project.count() == 1);
  }

  SECTION("Remove all layers") {
    auto *layer = new QgsVectorLayer("Point?crs=EPSG:4326", "temp", "memory");
    project.addMapLayer(layer);
    REQUIRE(project.count() == 1);

    project.removeAllMapLayers();
    REQUIRE(project.count() == 0);
  }
}

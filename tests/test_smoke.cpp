// Smoke tests — verify core classes can be instantiated
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsmapsettings.h>
#include <qgslayertree.h>

TEST_CASE("QgsProject can be created", "[smoke]") {
  auto *proj = new QgsProject();
  REQUIRE(proj != nullptr);
  REQUIRE(proj->title().isEmpty());
  delete proj;
}

TEST_CASE("QgsMapSettings defaults", "[smoke]") {
  QgsMapSettings settings;
  REQUIRE(settings.destinationCrs().isValid() == false); // no CRS set yet
  REQUIRE(settings.extent().isEmpty());
}

TEST_CASE("QgsLayerTree can be created", "[smoke]") {
  QgsLayerTree tree;
  REQUIRE(tree.children().isEmpty());
}

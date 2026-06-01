// Processing tests — verify algorithm providers and basic algorithm metadata
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingalgorithm.h>
#include <processing/providers/qgis_algorithms/provider.h>
#include <processing/providers/gdal_tools/provider.h>
#include <processing/providers/otb_tools/provider.h>

TEST_CASE("QgisAlgorithms provider loads", "[processing]") {
  QgisAlgorithmsProvider provider;
  REQUIRE(provider.id() == "qgis_algorithms");
  REQUIRE_FALSE(provider.name().isEmpty());
}

TEST_CASE("GdalTools provider loads", "[processing]") {
  GdalToolsProvider provider;
  REQUIRE(provider.id() == "gdal_tools");
  REQUIRE_FALSE(provider.name().isEmpty());
}

TEST_CASE("OtbTools provider loads", "[processing]") {
  OtbToolsProvider provider;
  REQUIRE(provider.id() == "otb_tools");
  REQUIRE_FALSE(provider.name().isEmpty());
}

TEST_CASE("QgisAlgorithms provider has algorithms", "[processing]") {
  QgisAlgorithmsProvider provider;
  provider.load();
  auto algs = provider.algorithms();
  REQUIRE_FALSE(algs.empty());
}

TEST_CASE("Processing providers registered in registry", "[processing][registry]") {
  // Register providers (same as main.cpp does)
  auto *registry = QgsApplication::processingRegistry();
  REQUIRE(registry != nullptr);

  // Register if not already present
  if (!registry->providerById("gdal_tools"))
    registry->addProvider(new GdalToolsProvider());
  if (!registry->providerById("otb_tools"))
    registry->addProvider(new OtbToolsProvider());
  if (!registry->providerById("qgis_algorithms"))
    registry->addProvider(new QgisAlgorithmsProvider());

  SECTION("GdalTools provider registered") {
    auto *provider = registry->providerById("gdal_tools");
    REQUIRE(provider != nullptr);
    CHECK(provider->id() == "gdal_tools");
  }

  SECTION("OtbTools provider registered") {
    auto *provider = registry->providerById("otb_tools");
    REQUIRE(provider != nullptr);
    CHECK(provider->id() == "otb_tools");
  }

  SECTION("QgisAlgorithms provider registered") {
    auto *provider = registry->providerById("qgis_algorithms");
    REQUIRE(provider != nullptr);
    CHECK(provider->id() == "qgis_algorithms");
  }

  SECTION("Registry has at least 3 providers") {
    CHECK(registry->providers().size() >= 3);
  }

  SECTION("Providers have algorithms after registration") {
    auto *gdal = registry->providerById("gdal_tools");
    REQUIRE(gdal != nullptr);
    CHECK(gdal->algorithms().size() > 0);

    auto *otb = registry->providerById("otb_tools");
    REQUIRE(otb != nullptr);
    CHECK(otb->algorithms().size() > 0);

    auto *qgis = registry->providerById("qgis_algorithms");
    REQUIRE(qgis != nullptr);
    CHECK(qgis->algorithms().size() > 0);
  }
}

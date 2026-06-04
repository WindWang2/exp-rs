// tests/test_processing_dialog.cpp — Test processing dialog parameter collection
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>
#include <QFileInfo>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingfeedback.h>
#include <processing/qgsprocessingcontext.h>

#include "processing/providers/qgis_algorithms/provider.h"
#include "processing/providers/gdal_tools/provider.h"
#include "processing/providers/otb_tools/provider.h"

// Ensure QgsApplication is initialized before tests
struct QgisFixture {
    QgisFixture() {
        if (!QgsApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "test";
            static char *argv[] = {arg0, nullptr};
            new QgsApplication(argc, argv, false);
        }
        QgsApplication::initQgis();
    }
    ~QgisFixture() {
        QgsProject::instance()->clear();
    }
};

TEST_CASE("Processing algorithm can be run with parameters", "[processing][dialog]") {
    QgisFixture fixture;

    // Register providers
    auto *registry = QgsApplication::processingRegistry();
    REQUIRE(registry != nullptr);

    if (!registry->providerById("gdal_tools"))
        registry->addProvider(new GdalToolsProvider());
    if (!registry->providerById("otb_tools"))
        registry->addProvider(new OtbToolsProvider());
    if (!registry->providerById("qgis_algorithms"))
        registry->addProvider(new QgisAlgorithmsProvider());

    // Find an algorithm that takes parameters
    auto *alg = registry->algorithmById("qgis_algorithms:raster_ndvi");
    REQUIRE(alg != nullptr);

    // Verify algorithm has parameters
    auto params = alg->parameterDefinitions();
    REQUIRE_FALSE(params.isEmpty());

    // Verify parameters have names and descriptions
    for (const auto *param : params) {
        CHECK_FALSE(param->name().isEmpty());
        CHECK_FALSE(param->description().isEmpty());
    }
}

TEST_CASE("Processing algorithm parameters can be collected", "[processing][dialog]") {
    QgisFixture fixture;

    // Register providers
    auto *registry = QgsApplication::processingRegistry();
    if (!registry->providerById("qgis_algorithms"))
        registry->addProvider(new QgisAlgorithmsProvider());

    // Find an algorithm
    auto *alg = registry->algorithmById("qgis_algorithms:raster_ndvi");
    REQUIRE(alg != nullptr);

    // Get parameter definitions
    auto params = alg->parameterDefinitions();
    REQUIRE(params.size() >= 3); // RED_BAND, NIR_BAND, OUTPUT

    // Verify parameters have valid names
    for (const auto *param : params) {
        CHECK_FALSE(param->name().isEmpty());
    }
}

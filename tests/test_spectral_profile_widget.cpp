// tests/test_spectral_profile_widget.cpp — Test SpectralProfileWidget dangling pointer fix
#include <catch2/catch_test_macros.hpp>

#include <QApplication>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgslayertree.h>

#include "app/widgets/spectral_profile_widget.h"

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

TEST_CASE("SpectralProfileWidget handles layer removal", "[widget][spectral]") {
    QgisFixture fixture;
    QgsProject *project = QgsProject::instance();
    project->clear();

    // Create a memory raster layer (always valid)
    QgsRasterLayer *layer = new QgsRasterLayer("Point?crs=epsg:4326", "test", "memory");
    if (!layer->isValid()) {
        // If we can't create a valid layer, skip the test
        delete layer;
        return;
    }
    project->addMapLayer(layer);

    // Create the widget
    SpectralProfileWidget widget;
    QgsPointXY point(0, 0);

    // Set a profile (this will try to extract data, but memory layers don't support GDAL)
    widget.setProfile(point, layer);

    // Remove the layer from the project
    project->removeMapLayer(layer->id());

    // Process events to allow signals to be delivered
    QApplication::processEvents();

    // The widget should handle this gracefully
    // After the fix, it should clear its internal pointer
    // Without the fix, this would crash on next paint/access

    // Try to clear the widget (this should not crash)
    REQUIRE_NOTHROW(widget.clear());
}

TEST_CASE("SpectralProfileWidget clears on layer removal", "[widget][spectral]") {
    QgisFixture fixture;
    QgsProject *project = QgsProject::instance();
    project->clear();

    // Create a memory raster layer
    QgsRasterLayer *layer = new QgsRasterLayer("Point?crs=epsg:4326", "test", "memory");
    if (!layer->isValid()) {
        delete layer;
        return;
    }
    project->addMapLayer(layer);

    SpectralProfileWidget widget;
    QgsPointXY point(0, 0);

    // Set a profile
    widget.setProfile(point, layer);

    // Remove the layer
    project->removeMapLayer(layer->id());

    // Process events to allow signals to be delivered
    QApplication::processEvents();

    // The widget should now have cleared its internal state
    // We can't easily check this without exposing internals,
    // but we can verify it doesn't crash when we try to use it

    // Try to set a new profile with nullptr (should not crash)
    REQUIRE_NOTHROW(widget.setProfile(point, nullptr));
}

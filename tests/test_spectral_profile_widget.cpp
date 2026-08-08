// tests/test_spectral_profile_widget.cpp — Test SpectralProfileWidget dangling pointer fix
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgslayertree.h>

#include <gdal.h>

#include "app/widgets/spectral_profile_widget.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <array>
#include <cmath>
#include <vector>

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

/// Create a 2-band 10x10 Float32 GeoTIFF with GT {0,1,0,0,0,-1} and band
/// descriptions "B2"/"B4". Band 1 = 10*y + x, band 2 = 100 + 10*y + x.
/// When @p withWavelengths, stamps WAVELENGTH band metadata (490 / 665 nm).
/// When @p withFwhm, stamps FWHM band metadata (45 / 60 nm).
QString makeTwoBandRaster(const QString &path, bool withWavelengths = false, bool withFwhm = false) {
    ensureGdalInit();
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH ds = createOutputTiff(path, 10, 10, 2, GDT_Float32, gt, QString());
    if (!ds)
        return QStringLiteral("createOutputTiff failed");
    for (int b = 0; b < 2; ++b) {
        std::vector<float> band(100);
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 10; ++x)
                band[static_cast<size_t>(y * 10 + x)] = static_cast<float>(b * 100 + y * 10 + x);
        if (GDALRasterIO(GDALGetRasterBand(ds, b + 1), GF_Write, 0, 0, 10, 10,
                         band.data(), 10, 10, GDT_Float32, 0, 0) != CE_None) {
            GDALClose(ds);
            return QStringLiteral("GDALRasterIO failed");
        }
        GDALSetDescription(GDALGetRasterBand(ds, b + 1), b == 0 ? "B2" : "B4");
        if (withWavelengths)
            GDALSetMetadataItem(GDALGetRasterBand(ds, b + 1), "WAVELENGTH",
                                b == 0 ? "490" : "665", nullptr);
        if (withFwhm)
            GDALSetMetadataItem(GDALGetRasterBand(ds, b + 1), "FWHM",
                                b == 0 ? "45" : "60", nullptr);
    }
    GDALClose(ds);
    return {};
}

TEST_CASE("SpectralProfileWidget extracts per-band values and labels", "[widget][spectral]") {
    QgisFixture fixture;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("two_band.tif"));
    REQUIRE(makeTwoBandRaster(path).isEmpty());

    QgsRasterLayer *layer = new QgsRasterLayer(path, QStringLiteral("profile"), "gdal");
    REQUIRE(layer->isValid());

    SpectralProfileWidget widget;
    // GT {0,1,0,0,0,-1}: map point (3.5, -2.5) -> pixel col 3, row 2.
    widget.setProfile(QgsPointXY(3.5, -2.5), layer);

    REQUIRE(widget.hasData());
    REQUIRE(widget.values().size() == 2);
    REQUIRE_THAT(widget.values()[0], Catch::Matchers::WithinAbs(23.0, 1e-3)); // 10*2+3
    REQUIRE_THAT(widget.values()[1], Catch::Matchers::WithinAbs(123.0, 1e-3)); // 100+10*2+3
    REQUIRE(widget.bandLabels().size() == 2);
    CHECK(widget.bandLabels()[0] == QStringLiteral("B2"));
    CHECK(widget.bandLabels()[1] == QStringLiteral("B4"));

    // A second point on the same layer reuses the cached dataset handle.
    widget.setProfile(QgsPointXY(7.5, -0.5), layer);
    REQUIRE(widget.hasData());
    REQUIRE_THAT(widget.values()[0], Catch::Matchers::WithinAbs(7.0, 1e-3));
    REQUIRE_THAT(widget.values()[1], Catch::Matchers::WithinAbs(107.0, 1e-3));

    delete layer;
}

TEST_CASE("SpectralProfileWidget out-of-bounds point yields no data", "[widget][spectral]") {
    QgisFixture fixture;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("two_band.tif"));
    REQUIRE(makeTwoBandRaster(path).isEmpty());

    QgsRasterLayer *layer = new QgsRasterLayer(path, QStringLiteral("profile"), "gdal");
    REQUIRE(layer->isValid());

    SpectralProfileWidget widget;
    widget.setProfile(QgsPointXY(500.0, 500.0), layer);
    CHECK_FALSE(widget.hasData());

    delete layer;
}

TEST_CASE("SpectralProfileWidget clears state on null layer", "[widget][spectral]") {
    QgisFixture fixture;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("two_band.tif"));
    REQUIRE(makeTwoBandRaster(path).isEmpty());

    QgsRasterLayer *layer = new QgsRasterLayer(path, QStringLiteral("profile"), "gdal");
    REQUIRE(layer->isValid());

    SpectralProfileWidget widget;
    widget.setProfile(QgsPointXY(1.5, -1.5), layer);
    REQUIRE(widget.hasData());

    widget.setProfile(QgsPointXY(1.5, -1.5), nullptr);
    CHECK_FALSE(widget.hasData());
    CHECK(widget.values().isEmpty());

    delete layer;
}

TEST_CASE("SpectralProfileWidget handles layer removal", "[widget][spectral]") {
    QgisFixture fixture;
    QgsProject *project = QgsProject::instance();
    project->clear();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // Real GeoTIFF layer so the profile actually extracts data.
    const QString path = dir.filePath(QStringLiteral("two_band.tif"));
    REQUIRE(makeTwoBandRaster(path).isEmpty());
    QgsRasterLayer *layer = new QgsRasterLayer(path, QStringLiteral("profile"), "gdal");
    REQUIRE(layer->isValid());
    project->addMapLayer(layer);

    SpectralProfileWidget widget;
    QgsPointXY point(1.5, -1.5);
    widget.setProfile(point, layer);
    REQUIRE(widget.hasData());

    // Remove the layer from the project
    project->removeMapLayer(layer->id());
    QApplication::processEvents();

    // The widget must not touch the removed layer: clearing and re-using must
    // not crash or read stale data.
    REQUIRE_NOTHROW(widget.clear());
    REQUIRE_NOTHROW(widget.setProfile(point, nullptr));
    CHECK_FALSE(widget.hasData());
}

TEST_CASE("SpectralProfileWidget clears on layer removal", "[widget][spectral]") {
    QgisFixture fixture;
    QgsProject *project = QgsProject::instance();
    project->clear();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("two_band.tif"));
    REQUIRE(makeTwoBandRaster(path).isEmpty());
    QgsRasterLayer *layer = new QgsRasterLayer(path, QStringLiteral("profile"), "gdal");
    REQUIRE(layer->isValid());
    project->addMapLayer(layer);

    SpectralProfileWidget widget;
    QgsPointXY point(1.5, -1.5);
    widget.setProfile(point, layer);
    REQUIRE(widget.hasData());

    // Remove the layer
    project->removeMapLayer(layer->id());
    QApplication::processEvents();

    // The widget keeps its own cached GDAL handle; a null-layer profile must
    // clear state without crashing.
    REQUIRE_NOTHROW(widget.setProfile(point, nullptr));
    CHECK_FALSE(widget.hasData());
}

TEST_CASE("SpectralProfileWidget exposes WAVELENGTH metadata wavelengths", "[widget][spectral]") {
    QgisFixture fixture;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    SECTION("Raster with WAVELENGTH metadata") {
        const QString path = dir.filePath(QStringLiteral("wl.tif"));
        REQUIRE(makeTwoBandRaster(path, /*withWavelengths=*/true).isEmpty());

        QgsRasterLayer *layer = new QgsRasterLayer(path, QStringLiteral("wl"), "gdal");
        REQUIRE(layer->isValid());

        SpectralProfileWidget widget;
        widget.setProfile(QgsPointXY(3.5, -2.5), layer);

        REQUIRE(widget.hasData());
        REQUIRE(widget.wavelengths().size() == 2);
        CHECK(widget.wavelengths()[0] == 490.0);
        CHECK(widget.wavelengths()[1] == 665.0);

        delete layer;
    }

    SECTION("Raster without WAVELENGTH metadata yields an empty grid") {
        const QString path = dir.filePath(QStringLiteral("plain.tif"));
        REQUIRE(makeTwoBandRaster(path).isEmpty());

        QgsRasterLayer *layer = new QgsRasterLayer(path, QStringLiteral("plain"), "gdal");
        REQUIRE(layer->isValid());

        SpectralProfileWidget widget;
        widget.setProfile(QgsPointXY(3.5, -2.5), layer);

        REQUIRE(widget.hasData());
        REQUIRE(widget.wavelengths().size() == 2);
        CHECK(widget.wavelengths()[0] == 0.0);
        CHECK(widget.wavelengths()[1] == 0.0);

        delete layer;
    }

    SECTION("clear() resets the wavelength grid") {
        const QString path = dir.filePath(QStringLiteral("wl2.tif"));
        REQUIRE(makeTwoBandRaster(path, true).isEmpty());

        QgsRasterLayer *layer = new QgsRasterLayer(path, QStringLiteral("wl2"), "gdal");
        REQUIRE(layer->isValid());

        SpectralProfileWidget widget;
        widget.setProfile(QgsPointXY(3.5, -2.5), layer);
        REQUIRE(widget.wavelengths().size() == 2);

        widget.clear();
        CHECK(widget.wavelengths().isEmpty());
        CHECK_FALSE(widget.hasData());

        delete layer;
    }
}

TEST_CASE("SpectralProfileWidget displays a precomputed spectrum (ROI mean)", "[widget][spectral]") {
    QgisFixture fixture;

    SpectralProfileWidget widget;
    CHECK_FALSE(widget.hasData());

    widget.setSpectrum({0.1, 0.2, 0.3}, {500.0, 600.0, 700.0}, {"B2", "B4", "B8"}, "roi_layer");

    CHECK(widget.hasData());
    REQUIRE(widget.values().size() == 3);
    CHECK(widget.values()[0] == Catch::Approx(0.1));
    CHECK(widget.values()[2] == Catch::Approx(0.3));
    REQUIRE(widget.wavelengths().size() == 3);
    CHECK(widget.wavelengths()[1] == Catch::Approx(600.0));
    REQUIRE(widget.bandLabels().size() == 3);
    CHECK(widget.bandLabels()[0] == QStringLiteral("B2"));

    // Mismatched auxiliary vectors are dropped without crashing.
    widget.setSpectrum({0.5, 0.6}, {1.0}, {});
    CHECK(widget.hasData());
    CHECK(widget.wavelengths().isEmpty());
    CHECK(widget.bandLabels().isEmpty());

    // An empty spectrum clears the widget.
    widget.setSpectrum({});
    CHECK_FALSE(widget.hasData());
}

TEST_CASE("SpectralProfileWidget continuum-removal view transforms the display", "[widget][spectral]") {
    QgisFixture fixture;

    SpectralProfileWidget widget;
    // Spectrum with an absorption dip at band 1: {0.5, 0.2, 0.4, 0.6}.
    widget.setSpectrum({0.5, 0.2, 0.4, 0.6});

    // Default: display == raw values.
    CHECK(widget.displayValues() == widget.values());
    CHECK_FALSE(widget.continuumRemovalEnabled());

    widget.setContinuumRemovalEnabled(true);
    CHECK(widget.continuumRemovalEnabled());
    const auto cr = widget.displayValues();
    REQUIRE(cr.size() == 4);
    for (double v : cr) {
        CHECK(v > 0.0);
        CHECK(v <= 1.0);
    }
    // The absorption dip survives continuum removal (band 1 stays the valley).
    CHECK(cr[1] < cr[0]);
    // Raw values are untouched by the display transform.
    CHECK(widget.values()[1] == Catch::Approx(0.2));

    widget.setContinuumRemovalEnabled(false);
    CHECK(widget.displayValues() == widget.values());
}

TEST_CASE("SpectralProfileWidget exposes FWHM band metadata", "[widget][spectral]") {
    QgisFixture fixture;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("fwhm.tif");
    REQUIRE(makeTwoBandRaster(path, false, /*withFwhm=*/true).isEmpty());

    auto *layer = new QgsRasterLayer(path, QStringLiteral("fwhm_layer"));
    REQUIRE(layer->isValid());

    SpectralProfileWidget widget;
    widget.setProfile(QgsPointXY(3.5, -2.5), layer);
    REQUIRE(widget.fwhm().size() == 2);
    CHECK(widget.fwhm()[0] == Catch::Approx(45.0));
    CHECK(widget.fwhm()[1] == Catch::Approx(60.0));

    widget.clear();
    CHECK(widget.fwhm().isEmpty());

    delete layer;
}

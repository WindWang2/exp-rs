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

TEST_CASE("Widgets/SpectralProfile: Layer destruction outside QgsProject resets pointer", "[widgets][spectral]") {
    QgisFixture fixture;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("standalone.tif");
    REQUIRE(makeTwoBandRaster(path).isEmpty());

    auto *layer = new QgsRasterLayer(path, QStringLiteral("standalone_layer"));
    REQUIRE(layer->isValid());

    SpectralProfileWidget widget;
    widget.setProfile(QgsPointXY(3.5, -2.5), layer);
    REQUIRE(widget.hasData());

    // Layer is destroyed directly (not via QgsProject)
    delete layer;

    // Widget reset / clear works cleanly
    widget.clear();
    CHECK_FALSE(widget.hasData());
}

// ─── D13 · exp_gui spectral workbench widgets ─────────────────────────────
// Runs under the shared QgisFixture (QgsApplication, headless). Truths come
// from GDAL-written synthetic rasters with known band constants and Qt
// lifecycle semantics — never from the widget implementation.
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QThread>

#include <qgspointxy.h>

#include "app/widgets/band_composite_palette.h"

namespace {

constexpr double kD13NoData = -9999.0;

/// Writes an N-band Float32 GeoTIFF with per-band constant value and
/// optional WAVELENGTH metadata (450 + 100·(b-1) nm).
QString writeD13MultibandRaster(const QTemporaryDir &dir, const QString &name, int bands,
                                const QVector<double> &bandValues, bool withWavelengths) {
    ensureGdalInit();
    const QString path = dir.filePath(name);
    // North-up 3x3 grid covering [0,3]x[0,3]: the D13 test points (1,1) and
    // (0.5,0.5) land inside. With the old origin (0,0) they mapped to row < 0
    // and every sample came back NaN -> hasData() never became true.
    GDALDatasetH ds = createOutputTiff(path, 3, 3, bands, GDT_Float32, {0.0, 1.0, 0.0, 3.0, 0.0, -1.0}, QString());
    if (!ds)
        return QString();
    for (int b = 1; b <= bands; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b);
        std::vector<float> line(3, static_cast<float>(bandValues[b - 1]));
        for (int row = 0; row < 3; ++row)
            GDALRasterIO(band, GF_Write, 0, row, 3, 1, line.data(), 3, 1, GDT_Float32, 0, 0);
        if (withWavelengths) {
            const double wavelengthNm = 450.0 + (b - 1) * 100.0;
            GDALSetMetadataItem(band, "WAVELENGTH", QString::number(wavelengthNm).toUtf8().constData(), nullptr);
        }
    }
    GDALClose(ds);
    return path;
}

/// Pumps the event loop until @p predicate holds or the timeout elapses.
bool d13WaitFor(const std::function<bool()> &predicate, int timeoutMs = 10000) {
    QDeadlineTimer deadline(timeoutMs);
    while (!predicate() && !deadline.hasExpired()) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    return predicate();
}

} // namespace

TEST_CASE("D13 profile widget data model: set, query, clear", "[gui][spectral_widget][d13]") {
    QgisFixture fixture;
    exp_gui::SpectralProfileWidget widget;

    REQUIRE_FALSE(widget.hasData());
    REQUIRE(widget.currentValues().isEmpty());

    QVector<double> values = {0.1, 0.2, 0.6, 0.7, 0.5};
    QVector<double> wavelengths = {450.0, 560.0, 660.0, 840.0, 1610.0};
    widget.setSpectrum(values, wavelengths, {}, QStringLiteral("fixture"));

    REQUIRE(widget.hasData());
    REQUIRE(widget.currentValues().size() == 5);
    REQUIRE(widget.currentWavelengths().size() == 5);
    REQUIRE(widget.currentWavelengths().back() == 1610.0);

    widget.clear();
    REQUIRE_FALSE(widget.hasData());
    REQUIRE(widget.currentValues().isEmpty());

    // Empty spectrum is an explicit clear.
    widget.setSpectrum(values, wavelengths);
    REQUIRE(widget.hasData());
    widget.setSpectrum({}, {});
    REQUIRE_FALSE(widget.hasData());
}

TEST_CASE("D13 profile widget renders offscreen inside a frame budget", "[gui][spectral_widget][d13]") {
    QgisFixture fixture;
    exp_gui::SpectralProfileWidget widget;
    widget.resize(800, 600);

    QPixmap empty(widget.size());
    widget.render(&empty);
    REQUIRE_FALSE(empty.isNull());

    QVector<double> values = {0.1, 0.2, 0.6, 0.7, 0.5};
    QVector<double> wavelengths = {450.0, 560.0, 660.0, 840.0, 1610.0};
    widget.setSpectrum(values, wavelengths);
    QPixmap pixmap(widget.size());
    widget.render(&pixmap);
    REQUIRE_FALSE(pixmap.isNull());

    // 1000-band polyline paint must stay inside the 30 ms frame budget.
    QVector<double> dense(1000);
    for (int i = 0; i < 1000; ++i)
        dense[i] = 0.2 + 0.5 * std::sin(i / 40.0);
    widget.setSpectrum(dense);
    QPixmap frame(widget.size());
    QElapsedTimer timer;
    timer.start();
    widget.render(&frame);
    const qint64 elapsedMs = timer.elapsed();
    INFO("1000-band paint took " << elapsedMs << " ms");
    REQUIRE(elapsedMs < 30);
}

TEST_CASE("D13 profile widget crosshair emits featureSelected", "[gui][spectral_widget][d13]") {
    QgisFixture fixture;
    exp_gui::SpectralProfileWidget widget;
    widget.resize(800, 600);

    widget.setSpectrum(QVector<double>{0.0, 1.0}, QVector<double>{500.0, 1500.0});

    QSignalSpy spy(&widget, &exp_gui::SpectralProfileWidget::featureSelected);
    REQUIRE(spy.isValid());

    QMouseEvent move(QEvent::MouseMove, QPointF(400.0, 300.0), QPointF(400.0, 300.0),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);

    REQUIRE(spy.count() >= 1);
    const QList<QVariant> args = spy.last();
    const double wavelength = args.at(0).toDouble();
    const double depth = args.at(1).toDouble();
    REQUIRE(wavelength > 500.0);
    REQUIRE(wavelength < 1500.0);
    REQUIRE(depth >= 0.0);
    REQUIRE(depth <= 1.0);
}

TEST_CASE("D13 profile widget async sampling lands band values and wavelength metadata",
          "[gui][spectral_widget][d13]") {
    QgisFixture fixture;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QVector<double> bandValues = {0.05, 0.12, 0.31};
    const QString path = writeD13MultibandRaster(dir, QStringLiteral("d13_async.tif"), 3, bandValues, true);
    REQUIRE(!path.isEmpty());

    auto layer = std::make_unique<QgsRasterLayer>(path, QStringLiteral("d13"));
    REQUIRE(layer->isValid());

    exp_gui::SpectralProfileWidget widget;
    widget.resize(800, 600);

    QSignalSpy readySpy(&widget, &exp_gui::SpectralProfileWidget::profileReady);
    QSignalSpy pointSpy(&widget, &exp_gui::SpectralProfileWidget::inspectionPointChanged);

    widget.setProfile(QgsPointXY(1.0, 1.0), layer.get());
    REQUIRE(pointSpy.count() == 1);

    REQUIRE(d13WaitFor([&] { return widget.hasData(); }));
    REQUIRE(readySpy.count() >= 1);
    REQUIRE(widget.currentValues().size() == 3);
    REQUIRE(widget.currentValues()[0] == Catch::Approx(0.05).margin(1e-5));
    REQUIRE(widget.currentValues()[1] == Catch::Approx(0.12).margin(1e-5));
    REQUIRE(widget.currentValues()[2] == Catch::Approx(0.31).margin(1e-5));
    REQUIRE(widget.currentWavelengths()[0] == Catch::Approx(450.0).margin(1e-3));
    REQUIRE(widget.currentWavelengths()[2] == Catch::Approx(650.0).margin(1e-3));
}

TEST_CASE("D13 profile widget QPointer safety: layer destruction clears the widget",
          "[gui][spectral_widget][d13]") {
    QgisFixture fixture;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = writeD13MultibandRaster(dir, QStringLiteral("d13_lifecycle.tif"), 2,
                                                 {0.2, 0.4}, false);
    REQUIRE(!path.isEmpty());

    auto layer = std::make_unique<QgsRasterLayer>(path, QStringLiteral("d13"));
    REQUIRE(layer->isValid());

    exp_gui::SpectralProfileWidget widget;
    widget.setProfile(QgsPointXY(0.5, 0.5), layer.get());
    REQUIRE(d13WaitFor([&] { return widget.hasData(); }));

    // Destroy the layer: the widget clears gracefully — no UAF, no crash.
    QPointer<QgsRasterLayer> observer = layer.get();
    layer.reset();
    QApplication::processEvents();
    REQUIRE(observer.isNull());
    REQUIRE_FALSE(widget.hasData());
    REQUIRE(widget.currentValues().isEmpty());

    QPixmap pixmap(widget.size());
    widget.render(&pixmap);
    REQUIRE_FALSE(pixmap.isNull());
}

TEST_CASE("D13 band composite palette binds, maps and clamps safely", "[gui][spectral_widget][d13]") {
    QgisFixture fixture;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = writeD13MultibandRaster(dir, QStringLiteral("d13_palette.tif"), 5,
                                                 {0.1, 0.2, 0.3, 0.4, 0.5}, false);
    REQUIRE(!path.isEmpty());

    auto layer = std::make_unique<QgsRasterLayer>(path, QStringLiteral("d13"));
    REQUIRE(layer->isValid());

    exp_gui::BandCompositePalette palette;
    REQUIRE(palette.bandCount() == 0);
    palette.bindRasterLayer(layer.get());
    REQUIRE(palette.hasLayer());
    REQUIRE(palette.bandCount() == 5);

    QSignalSpy mapSpy(&palette, &exp_gui::BandCompositePalette::bandMappingChanged);
    QSignalSpy stretchSpy(&palette, &exp_gui::BandCompositePalette::stretchMethodChanged);

    palette.setRgbMapping(3, 2, 1);
    REQUIRE(mapSpy.count() == 1);
    REQUIRE(mapSpy.first().at(0).toInt() == 3);
    REQUIRE(mapSpy.first().at(1).toInt() == 2);
    REQUIRE(mapSpy.first().at(2).toInt() == 1);

    // Illegal band numbers clamp into [0, bandCount] — no crash.
    palette.setRgbMapping(99, -4, 0);
    REQUIRE(palette.redBand() == 5);
    REQUIRE(palette.greenBand() == 0);
    REQUIRE(palette.blueBand() == 0);

    palette.setStretchMethod(2, 2.5);
    REQUIRE(stretchSpy.count() == 1);
    REQUIRE(stretchSpy.first().at(0).toInt() == 2);
    REQUIRE(stretchSpy.first().at(1).toDouble() == Catch::Approx(2.5));

    // Layer destruction leaves an inert palette — no crash, no signal.
    layer.reset();
    QApplication::processEvents();
    REQUIRE_FALSE(palette.hasLayer());
    const int signalsBefore = mapSpy.count();
    palette.setRgbMapping(1, 2, 3);
    // Inert after destruction: the request is refused without a new signal
    // (the earlier in-range and clamped calls already emitted legitimately).
    REQUIRE(mapSpy.count() == signalsBefore);
}

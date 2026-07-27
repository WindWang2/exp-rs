// Histogram Stretch Widget tests — verify UI controls and layer interaction
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <QComboBox>
#include <QFile>
#include <QList>
#include <QDoubleSpinBox>

#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsmultibandcolorrenderer.h>
#include <qgscontrastenhancement.h>
#include <qgsrasterblock.h>
#include <qgsmapsettings.h>
#include <qgsmaprenderersequentialjob.h>

#include "app/widgets/histogram_stretch_widget.h"
#include "app/widgets/histogram_widget.h"

namespace {

QString samplePath( const char *name )
{
    // Try cwd and common repo-relative locations (tests often run from build/).
    const QStringList candidates = {
        QStringLiteral( "data/samples/%1" ).arg( name ),
        QStringLiteral( "../data/samples/%1" ).arg( name ),
        QStringLiteral( "../../data/samples/%1" ).arg( name ),
        QStringLiteral( "data/%1" ).arg( name ),
        QStringLiteral( "../data/%1" ).arg( name ),
    };
    for ( const QString &p : candidates )
    {
        if ( QFile::exists( p ) )
            return p;
    }
    return candidates.first();
}

} // namespace

TEST_CASE("HistogramStretchWidget creation and defaults", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;
    CHECK(widget.rasterLayer() == nullptr);
    CHECK(widget.band() == 1);
    CHECK(widget.algorithm() == HistogramStretchWidget::StretchAlgorithm::PiecewiseLinear);
}

TEST_CASE("HistogramStretchWidget setRasterLayer", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;

    auto *layer = new QgsRasterLayer(samplePath("dem_sample.tif"), QStringLiteral("dem"));
    if (layer->isValid()) {
        widget.setRasterLayer(layer);
        CHECK(widget.rasterLayer() == layer);
        CHECK(widget.band() == 1);
    } else {
        WARN("dem_sample.tif not available, skipping raster test");
    }

    delete layer;
    // QPointer must clear after layer destruction (no use-after-free on next apply).
    CHECK(widget.rasterLayer() == nullptr);
    widget.applyStretch(); // must not crash
    widget.resetStretch();
}

TEST_CASE("HistogramStretchWidget algorithm switching", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;

    auto *layer = new QgsRasterLayer(samplePath("dem_sample.tif"), QStringLiteral("dem"));
    if (layer->isValid()) {
        widget.setRasterLayer(layer);

        QSignalSpy spy(&widget, &HistogramStretchWidget::stretchApplied);
        REQUIRE(spy.isValid());

        // Find the stretch algorithm combo (not channel/band).
        QList<QComboBox *> combos = widget.findChildren<QComboBox *>();
        REQUIRE(combos.size() >= 3);
        QComboBox *algorithmCombo = combos.at(2); // channel, band, algorithm

        // Switch through all stretch algorithms — regression for crash on 增强/算法切换.
        for ( int i = 0; i < algorithmCombo->count(); ++i )
        {
            algorithmCombo->setCurrentIndex( i );
            widget.applyStretch();
            CHECK( layer->isValid() );
            CHECK( layer->renderer() != nullptr );
        }
        CHECK( spy.count() >= algorithmCombo->count() );
    } else {
        WARN("dem_sample.tif not available, skipping algorithm test");
    }

    delete layer;
    CHECK(widget.rasterLayer() == nullptr);
    widget.applyStretch(); // must not crash with dangling layer cleared
}

TEST_CASE("HistogramStretchWidget band selection", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;

    auto *layer = new QgsRasterLayer(samplePath("landsat_sample.tif"), QStringLiteral("landsat"));
    if (layer->isValid() && layer->bandCount() >= 3) {
        widget.setRasterLayer(layer);
        widget.setBand(2);
        CHECK(widget.band() == 2);
        widget.setBand(3);
        CHECK(widget.band() == 3);
    } else {
        WARN("landsat_sample.tif not available, skipping band test");
    }

    delete layer;
}

TEST_CASE("HistogramStretchWidget normalizes a master piecewise curve per RGB band", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    auto *layer = new QgsRasterLayer(samplePath("landsat_sample.tif"), QStringLiteral("landsat"));
    if ( !layer->isValid() || layer->bandCount() < 3 ) {
        WARN("landsat_sample.tif not available, skipping RGB piecewise regression test");
        delete layer;
        return;
    }

    layer->setRenderer(new QgsMultiBandColorRenderer(layer->dataProvider(), 1, 2, 3));

    HistogramStretchWidget widget;
    widget.setRasterLayer(layer);
    widget.applyStretch();

    auto *renderer = dynamic_cast<QgsMultiBandColorRenderer *>(layer->renderer());
    REQUIRE(renderer != nullptr);
    REQUIRE(renderer->blueContrastEnhancement() != nullptr);

    const QgsRasterBandStats blueStats = layer->dataProvider()->bandStatistics(
        3, Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max);
    const double blueMidpoint = (blueStats.minimumValue + blueStats.maximumValue) / 2.0;

    // The default master curve is a straight 0..255 line. It must use the
    // blue band's own physical range, not clamp values above band 1's maximum.
    auto *blueEnhancement = const_cast<QgsContrastEnhancement *>(renderer->blueContrastEnhancement());
    CHECK(blueEnhancement->enhanceContrast(blueMidpoint) ==
          Catch::Approx(128).margin(2));

    delete layer;
}

TEST_CASE("HistogramStretchWidget renders CCD1 piecewise output without a white fill", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    auto *layer = new QgsRasterLayer(samplePath("CCD1.dat"), QStringLiteral("CCD1"));
    if ( !layer->isValid() || layer->bandCount() < 3 ) {
        WARN("CCD1.dat not available, skipping CCD1 rendering regression test");
        delete layer;
        return;
    }

    layer->setRenderer(new QgsMultiBandColorRenderer(layer->dataProvider(), 1, 2, 3));
    HistogramStretchWidget widget;
    widget.setRasterLayer(layer);

    // Approximate the curve shown in the reported screenshot: an early, bright
    // control point followed by the maximum endpoint.
    auto *histogram = widget.findChild<HistogramWidget *>();
    REQUIRE(histogram != nullptr);
    histogram->setPiecewisePoints({ QPointF(12.0, 0.0), QPointF(25.0, 200.0), QPointF(230.0, 255.0) });
    widget.applyStretch();

    std::unique_ptr<QgsRasterBlock> block(layer->renderer()->block(1, layer->extent(), 64, 64));
    REQUIRE(block != nullptr);
    REQUIRE(block->isValid());
    const QRgb *pixels = block->colorData();
    REQUIRE(pixels != nullptr);

    int whitePixels = 0;
    int totalIntensity = 0;
    constexpr int pixelCount = 64 * 64;
    for (int i = 0; i < pixelCount; ++i)
    {
        totalIntensity += qRed(pixels[i]) + qGreen(pixels[i]) + qBlue(pixels[i]);
        if (qRed(pixels[i]) == 255 && qGreen(pixels[i]) == 255 && qBlue(pixels[i]) == 255)
            ++whitePixels;
    }

    CHECK(whitePixels < pixelCount * 0.98);
    CHECK(totalIntensity / (pixelCount * 3.0) < 255.0);

    delete layer;
}

TEST_CASE("Custom enhancement survives the renderer clone used by map rendering", "[gui][histogram][renderer-clone]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    auto *layer = new QgsRasterLayer(samplePath("CCD1.dat"), QStringLiteral("CCD1"));
    REQUIRE(layer->isValid());
    REQUIRE(layer->bandCount() >= 3);
    layer->setRenderer(new QgsMultiBandColorRenderer(layer->dataProvider(), 1, 2, 3));

    HistogramStretchWidget widget;
    widget.setRasterLayer(layer);

    SECTION("piecewise linear")
    {
        auto *histogram = widget.findChild<HistogramWidget *>();
        REQUIRE(histogram != nullptr);
        histogram->setPiecewisePoints(
            { QPointF(12.0, 0.0), QPointF(25.0, 200.0), QPointF(230.0, 255.0) } );
        widget.applyStretch();
    }

    SECTION("histogram equalization")
    {
        const QList<QComboBox *> combos = widget.findChildren<QComboBox *>();
        REQUIRE(combos.size() >= 3);
        combos.at(2)->setCurrentIndex(5);
    }

    auto *liveRenderer =
        dynamic_cast<QgsMultiBandColorRenderer *>(layer->renderer());
    REQUIRE(liveRenderer != nullptr);
    auto *liveEnhancement = const_cast<QgsContrastEnhancement *>(
        liveRenderer->redContrastEnhancement());
    REQUIRE(liveEnhancement != nullptr);
    REQUIRE(liveEnhancement->isValueInDisplayableRange(34.0));

    std::unique_ptr<QgsRasterRenderer> cloned(layer->renderer()->clone());
    auto *clonedRenderer =
        dynamic_cast<QgsMultiBandColorRenderer *>(cloned.get());
    REQUIRE(clonedRenderer != nullptr);
    auto *clonedEnhancement = const_cast<QgsContrastEnhancement *>(
        clonedRenderer->redContrastEnhancement());
    REQUIRE(clonedEnhancement != nullptr);

    CHECK(clonedEnhancement->isValueInDisplayableRange(34.0));
    CHECK(clonedEnhancement->enhanceContrast(34.0) ==
          liveEnhancement->enhanceContrast(34.0));

    const QColor background(QStringLiteral("#e9ecf0"));
    QgsMapSettings settings;
    settings.setLayers({layer});
    settings.setDestinationCrs(layer->crs());
    settings.setExtent(layer->extent());
    settings.setOutputSize(QSize(128, 128));
    settings.setBackgroundColor(background);

    QgsMapRendererSequentialJob renderJob(settings);
    renderJob.start();
    renderJob.waitForFinished();
    const QImage image =
        renderJob.renderedImage().convertToFormat(QImage::Format_ARGB32);
    REQUIRE_FALSE(image.isNull());

    int backgroundPixels = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb *line =
            reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            if (QColor::fromRgba(line[x]).rgb() == background.rgb())
                ++backgroundPixels;
        }
    }
    CAPTURE(backgroundPixels);
    CHECK(backgroundPixels < image.width() * image.height() * 0.98);

    delete layer;
}

TEST_CASE("HistogramStretchWidget applies Photoshop gamma", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    auto *layer = new QgsRasterLayer(samplePath("CCD1.dat"), QStringLiteral("CCD1"));
    REQUIRE(layer->isValid());
    layer->setRenderer(new QgsMultiBandColorRenderer(layer->dataProvider(), 1, 2, 3));

    HistogramStretchWidget widget;
    widget.setRasterLayer(layer);
    const QList<QComboBox *> combos = widget.findChildren<QComboBox *>();
    REQUIRE(combos.size() >= 3);
    combos.at(2)->setCurrentIndex(1); // Photoshop Levels

    QDoubleSpinBox *gammaSpin = nullptr;
    for (QDoubleSpinBox *spin : widget.findChildren<QDoubleSpinBox *>())
    {
        if (spin->minimum() == Catch::Approx(0.1) && spin->maximum() == Catch::Approx(10.0))
            gammaSpin = spin;
    }
    REQUIRE(gammaSpin != nullptr);
    gammaSpin->setValue(2.0);

    auto *renderer = dynamic_cast<QgsMultiBandColorRenderer *>(layer->renderer());
    REQUIRE(renderer != nullptr);
    auto *enhancement = const_cast<QgsContrastEnhancement *>(renderer->redContrastEnhancement());
    REQUIRE(enhancement != nullptr);
    CHECK(enhancement->enhanceContrast(121.0) > 170);
    delete layer;
}

TEST_CASE("HistogramStretchWidget histogram equalization differs from linear stretch", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    auto *layer = new QgsRasterLayer(samplePath("CCD1.dat"), QStringLiteral("CCD1"));
    REQUIRE(layer->isValid());
    layer->setRenderer(new QgsMultiBandColorRenderer(layer->dataProvider(), 1, 2, 3));

    HistogramStretchWidget widget;
    widget.setRasterLayer(layer);
    const QList<QComboBox *> combos = widget.findChildren<QComboBox *>();
    REQUIRE(combos.size() >= 3);
    QComboBox *algorithmCombo = combos.at(2);

    algorithmCombo->setCurrentIndex(4); // Linear Min-Max
    auto *linearRenderer = dynamic_cast<QgsMultiBandColorRenderer *>(layer->renderer());
    REQUIRE(linearRenderer != nullptr);
    auto *linearEnhancement = const_cast<QgsContrastEnhancement *>(linearRenderer->redContrastEnhancement());
    REQUIRE(linearEnhancement != nullptr);
    const int linearValue = linearEnhancement->enhanceContrast(34.0);

    algorithmCombo->setCurrentIndex(5); // Histogram Equalization
    auto *equalizedRenderer = dynamic_cast<QgsMultiBandColorRenderer *>(layer->renderer());
    REQUIRE(equalizedRenderer != nullptr);
    auto *equalizedEnhancement = const_cast<QgsContrastEnhancement *>(equalizedRenderer->redContrastEnhancement());
    REQUIRE(equalizedEnhancement != nullptr);
    CHECK(equalizedEnhancement->enhanceContrast(34.0) != linearValue);
    delete layer;
}

TEST_CASE("HistogramStretchWidget uses cumulative clipping and two sigma defaults", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    auto *layer = new QgsRasterLayer(samplePath("CCD1.dat"), QStringLiteral("CCD1"));
    REQUIRE(layer->isValid());
    layer->setRenderer(new QgsMultiBandColorRenderer(layer->dataProvider(), 1, 2, 3));

    HistogramStretchWidget widget;
    widget.setRasterLayer(layer);
    const QList<QComboBox *> combos = widget.findChildren<QComboBox *>();
    REQUIRE(combos.size() >= 3);
    QComboBox *algorithmCombo = combos.at(2);

    algorithmCombo->setCurrentIndex(2); // 2% cumulative clip
    auto *clipRenderer = dynamic_cast<QgsMultiBandColorRenderer *>(layer->renderer());
    REQUIRE(clipRenderer != nullptr);
    const QgsContrastEnhancement *clip = clipRenderer->redContrastEnhancement();
    REQUIRE(clip != nullptr);
    CHECK(clip->maximumValue() < 200.0);

    algorithmCombo->setCurrentIndex(3); // 2 sigma
    auto *stdDevRenderer = dynamic_cast<QgsMultiBandColorRenderer *>(layer->renderer());
    REQUIRE(stdDevRenderer != nullptr);
    const QgsContrastEnhancement *stdDev = stdDevRenderer->redContrastEnhancement();
    REQUIRE(stdDev != nullptr);
    CHECK(stdDev->maximumValue() > 70.0);
    CHECK(stdDev->maximumValue() < 90.0);
    delete layer;
}

TEST_CASE("HistogramStretchWidget reset removes stale piecewise points", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    auto *layer = new QgsRasterLayer(samplePath("CCD1.dat"), QStringLiteral("CCD1"));
    REQUIRE(layer->isValid());
    HistogramStretchWidget widget;
    widget.setRasterLayer(layer);

    auto *histogram = widget.findChild<HistogramWidget *>();
    REQUIRE(histogram != nullptr);
    histogram->setPiecewisePoints({ QPointF(12.0, 255.0), QPointF(25.0, 255.0), QPointF(230.0, 255.0) });
    widget.resetStretch();

    const QVector<QPointF> points = widget.piecewisePoints();
    REQUIRE(points.size() == 2);
    CHECK(points.front().y() == Catch::Approx(0.0));
    CHECK(points.back().y() == Catch::Approx(255.0));
    delete layer;
}

TEST_CASE("HistogramStretchWidget red channel does not alter green and blue", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    auto *layer = new QgsRasterLayer(samplePath("CCD1.dat"), QStringLiteral("CCD1"));
    REQUIRE(layer->isValid());
    layer->setRenderer(new QgsMultiBandColorRenderer(layer->dataProvider(), 1, 2, 3));

    HistogramStretchWidget widget;
    widget.setRasterLayer(layer);
    const QList<QComboBox *> combos = widget.findChildren<QComboBox *>();
    REQUIRE(combos.size() >= 3);
    combos.at(0)->setCurrentIndex(1); // Red
    widget.applyStretch();

    auto *renderer = dynamic_cast<QgsMultiBandColorRenderer *>(layer->renderer());
    REQUIRE(renderer != nullptr);
    REQUIRE(renderer->redContrastEnhancement() != nullptr);
    CHECK(renderer->greenContrastEnhancement() == nullptr);
    CHECK(renderer->blueContrastEnhancement() == nullptr);
    delete layer;
}

TEST_CASE("HistogramStretchWidget resetStretch", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;

    auto *layer = new QgsRasterLayer(samplePath("dem_sample.tif"), QStringLiteral("dem"));
    if (layer->isValid()) {
        widget.setRasterLayer(layer);
        widget.resetStretch();
        // After reset, min/max should be restored to data range
        // (verify no crash and layer remains valid)
        CHECK(widget.rasterLayer() == layer);
    } else {
        WARN("dem_sample.tif not available, skipping reset test");
    }

    delete layer;
}

TEST_CASE("HistogramStretchWidget survives deleted layer", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;
    auto *layer = new QgsRasterLayer(samplePath("dem_sample.tif"), QStringLiteral("dem"));
    if ( !layer->isValid() )
    {
        WARN("dem_sample.tif not available, skipping deleted-layer test");
        delete layer;
        return;
    }

    widget.setRasterLayer( layer );
    widget.applyStretch();

    // Simulate layer removal while panel still open (dangling-pointer crash path).
    delete layer;
    QApplication::processEvents();

    CHECK( widget.rasterLayer() == nullptr );
    widget.applyStretch();
    widget.resetStretch();
    // Emit-like path that previously crashed in applyPiecewiseEnhancement
    widget.applyStretch();
}

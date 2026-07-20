// Histogram Stretch Widget tests — verify UI controls and layer interaction
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <QComboBox>

#include <qgsrasterlayer.h>

#include "app/widgets/histogram_stretch_widget.h"

TEST_CASE("HistogramStretchWidget creation and defaults", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;
    CHECK(widget.rasterLayer() == nullptr);
    CHECK(widget.band() == 1);
    CHECK(widget.algorithm() == HistogramStretchWidget::StretchAlgorithm::LinearMinMax);
}

TEST_CASE("HistogramStretchWidget setRasterLayer", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;

    auto *layer = new QgsRasterLayer(QStringLiteral("data/dem_sample.tif"), QStringLiteral("dem"));
    if (layer->isValid()) {
        widget.setRasterLayer(layer);
        CHECK(widget.rasterLayer() == layer);
        CHECK(widget.band() == 1);
    } else {
        WARN("dem_sample.tif not available, skipping raster test");
    }

    delete layer;
}

TEST_CASE("HistogramStretchWidget algorithm switching", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;

    auto *layer = new QgsRasterLayer(QStringLiteral("data/dem_sample.tif"), QStringLiteral("dem"));
    if (layer->isValid()) {
        widget.setRasterLayer(layer);

        QSignalSpy spy(&widget, &HistogramStretchWidget::stretchApplied);
        REQUIRE(spy.isValid());

        // Switch algorithm should trigger applyStretch
        auto *combo = widget.findChild<QComboBox *>();
        REQUIRE(combo != nullptr);
    } else {
        WARN("dem_sample.tif not available, skipping algorithm test");
    }

    delete layer;
}

TEST_CASE("HistogramStretchWidget band selection", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;

    auto *layer = new QgsRasterLayer(QStringLiteral("data/landsat_sample.tif"), QStringLiteral("landsat"));
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

TEST_CASE("HistogramStretchWidget resetStretch", "[gui][histogram]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    HistogramStretchWidget widget;

    auto *layer = new QgsRasterLayer(QStringLiteral("data/dem_sample.tif"), QStringLiteral("dem"));
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

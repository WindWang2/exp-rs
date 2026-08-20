// test_roi_statistics_widget.cpp — ROI Statistics Widget point-in-polygon & NoData filtering (#429)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include "app/widgets/roi_statistics_widget.h"
#include "processing/algorithms/math_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <qgsapplication.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgspointxy.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <gdal.h>

#include <array>
#include <memory>
#include <vector>

using Catch::Approx;

namespace {

struct TestAppFixture {
    TestAppFixture() {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char appName[] = "test_roi_statistics_widget";
            static char *argv[] = { appName, nullptr };
            s_app = new QApplication(argc, argv);
            QgsApplication::initQgis();
        }
    }
    static QApplication *s_app;
};
QApplication *TestAppFixture::s_app = nullptr;

/// 4x4 raster with 2 bands:
/// Band 1: row * 10 + col, with pixel (0,0) = NoData (-999)
/// Band 2: 100 + row * 10 + col, no NoData pixels
/// (GTiff stores a single dataset-level NoData, so both bands share -999.)
QString makeTestRaster(const QString &path)
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::vector<std::vector<float>> bands(2, std::vector<float>(16, 0.0f));
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            bands[0][static_cast<size_t>(row * 4 + col)] = static_cast<float>(row * 10 + col);
            bands[1][static_cast<size_t>(row * 4 + col)] = 100.0f + static_cast<float>(row * 10 + col);
        }
    }
    bands[0][0] = -999.0f;
    QString err;
    if (!writeGdalOutput(path, 4, 4, bands, gt, QStringLiteral("EPSG:32648"), &err))
        return err;

    GDALDatasetH ds = GDALOpen(path.toUtf8().constData(), GA_Update);
    if (!ds) return QStringLiteral("Failed to open raster for updating NoData");
    GDALSetRasterNoDataValue(GDALGetRasterBand(ds, 1), -999.0);
    GDALClose(ds);
    return {};
}

} // namespace

TEST_CASE("RoiStatisticsWidget tests polygon containment and NoData filtering (#429)", "[app][widgets][roi_stats][429]")
{
    TestAppFixture fixture;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString rasterPath = tmp.filePath(QStringLiteral("test_roi_stats.tif"));
    REQUIRE(makeTestRaster(rasterPath).isEmpty());

    auto rasterLayer = std::make_unique<QgsRasterLayer>(rasterPath, QStringLiteral("test_raster"));
    REQUIRE(rasterLayer->isValid());

    RoiStatisticsWidget widget;
    widget.setRasterLayer(rasterLayer.get());

    SECTION("Without ROI layer, statistics are computed over full extent with NoData filtered")
    {
        widget.setRoiLayer(nullptr);
        widget.computeStatistics();

        auto stats = widget.statistics();
        REQUIRE(stats.size() == 2);

        // Band 1: 16 total pixels, pixel (0,0) = -999 is NoData -> 15 valid pixels
        CHECK(stats[0].pixelCount == 15);
        // Min should be 1.0 (pixel 0 is NoData), Max should be 33.0
        CHECK(stats[0].min == Approx(1.0));
        CHECK(stats[0].max == Approx(33.0));

        // Band 2: No pixels match NoData (-999.0) -> 16 valid pixels, min 100.0, max 133.0
        CHECK(stats[1].pixelCount == 16);
        CHECK(stats[1].min == Approx(100.0));
        CHECK(stats[1].max == Approx(133.0));
    }

    SECTION("With polygon ROI, statistics only include pixels inside the polygon")
    {
        // Polygon covers rectangle [0, 2] in map X and [0, -2] in map Y (cols 0,1; rows 0,1)
        // Pixels inside bbox: (row 0, col 0)=-999 (NoData), (row 0, col 1)=1, (row 1, col 0)=10, (row 1, col 1)=11
        auto roiLayer = std::make_unique<QgsVectorLayer>(QStringLiteral("Polygon?crs=EPSG:32648"), QStringLiteral("roi"), QStringLiteral("memory"));
        REQUIRE(roiLayer->isValid());

        QgsFeature feat;
        QgsGeometry geom = QgsGeometry::fromPolygonXY({{
            QgsPointXY(0.0, 0.0),
            QgsPointXY(2.0, 0.0),
            QgsPointXY(2.0, -2.0),
            QgsPointXY(0.0, -2.0)
        }});
        feat.setGeometry(geom);
        QgsFeatureList flist{feat};
        roiLayer->dataProvider()->addFeatures(flist);

        widget.setRoiLayer(roiLayer.get());
        widget.computeStatistics();

        auto stats = widget.statistics();
        REQUIRE(stats.size() == 2);

        // Band 1: 4 pixels in polygon, but (0,0)=0 is NoData -> 3 valid pixels {1, 10, 11}
        // Mean = (1 + 10 + 11) / 3 = 22 / 3 ≈ 7.3333
        CHECK(stats[0].pixelCount == 3);
        CHECK(stats[0].min == Approx(1.0));
        CHECK(stats[0].max == Approx(11.0));
        CHECK(stats[0].mean == Approx(22.0 / 3.0).margin(1e-3));

        // Band 2: 4 pixels in polygon {100, 101, 110, 111}
        // Mean = 422 / 4 = 105.5
        CHECK(stats[1].pixelCount == 4);
        CHECK(stats[1].min == Approx(100.0));
        CHECK(stats[1].max == Approx(111.0));
        CHECK(stats[1].mean == Approx(105.5).margin(1e-3));
    }

    SECTION("Triangle ROI excludes pixels outside triangle even within bounding box")
    {
        // Triangle with vertices (0,0), (3,0), (0,-3)
        auto roiLayer = std::make_unique<QgsVectorLayer>(QStringLiteral("Polygon?crs=EPSG:32648"), QStringLiteral("roi_tri"), QStringLiteral("memory"));
        REQUIRE(roiLayer->isValid());

        QgsFeature feat;
        QgsGeometry geom = QgsGeometry::fromPolygonXY({{
            QgsPointXY(0.0, 0.0),
            QgsPointXY(3.0, 0.0),
            QgsPointXY(0.0, -3.0)
        }});
        feat.setGeometry(geom);
        QgsFeatureList flist{feat};
        roiLayer->dataProvider()->addFeatures(flist);

        widget.setRoiLayer(roiLayer.get());
        widget.computeStatistics();

        auto stats = widget.statistics();
        REQUIRE(stats.size() == 2);
        // Pixel (3,3) center (3.5, -3.5) is outside -> excluded
        CHECK(stats[0].max < 33.0);
    }
}

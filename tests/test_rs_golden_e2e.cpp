// tests/test_rs_golden_e2e.cpp — Production E2E Golden Pipeline Test
//
// End-to-end integration test verifying optical remote sensing production workflows
// as a single sequential pipeline (no SECTION isolation):
// 1. Synthetic dataset creation (4-band bi-temporal imagery & DEM in EPSG:32650).
// 2. Categorical Raster Resampling Protection Verification.
// 3. Multi-band MAD Change Detection (CCA Chi-Square change magnitude).
// 4. Post-Classification Sieve / Majority Filter / Recode & Polygonization.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QTemporaryDir>
#include <QFile>

#include <qgsapplication.h>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/gdal/gdal_operator_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/algorithms/change_detection.h"
#include "analysis/classification/rs_post_process.h"

#include <json/json.h>
#include <opencv2/core.hpp>

#include <array>
#include <vector>

using Catch::Approx;
using namespace sicnu::operators;
using namespace sicnu::operators::gdal::util;

namespace {

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_rs_golden_e2e";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE("Optical Remote Sensing Production Golden E2E Workflow", "[e2e][rs_golden]")
{
    ensureQgisApplication();
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());

    const QString t0Path = tmpDir.filePath("t0_multiband.tif");
    const QString t1Path = tmpDir.filePath("t1_multiband.tif");
    const QString demPath = tmpDir.filePath("dem.tif");
    const QString changeOutPath = tmpDir.filePath("mad_change.tif");
    const QString labelT0Path = tmpDir.filePath("t0_labels.tif");
    const QString postClassPath = tmpDir.filePath("t0_post_labels.tif");
    const QString polyVectorPath = tmpDir.filePath("t0_polygons.gpkg");

    constexpr int width = 20;
    constexpr int height = 20;
    constexpr int pixelCount = width * height;
    constexpr int bandCount = 4;
    std::array<double, 6> gt = {500000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0};
    const QString crsWkt = QStringLiteral("EPSG:32650");

    // ========================================================================
    // Stage 1: Generate synthetic 4-band bi-temporal imagery & DEM
    // ========================================================================
    std::vector<std::vector<float>> t0Bands(bandCount, std::vector<float>(pixelCount));
    std::vector<std::vector<float>> t1Bands(bandCount, std::vector<float>(pixelCount));
    std::vector<std::vector<float>> demBands(1, std::vector<float>(pixelCount, 100.0f));

    for (int i = 0; i < pixelCount; ++i) {
        float base = static_cast<float>(i + 1);
        t0Bands[0][i] = base * 5.0f;
        t0Bands[1][i] = base * 10.0f;
        t0Bands[2][i] = base * 15.0f;
        t0Bands[3][i] = base * 20.0f;

        if (i >= 380) { // Localized change at the last 20 pixels
            t1Bands[0][i] = base * 5.0f + 150.0f;
            t1Bands[1][i] = base * 10.0f - 80.0f;
            t1Bands[2][i] = base * 15.0f + 100.0f;
            t1Bands[3][i] = base * 20.0f - 50.0f;
        } else {
            t1Bands[0][i] = base * 5.0f;
            t1Bands[1][i] = base * 10.0f;
            t1Bands[2][i] = base * 15.0f;
            t1Bands[3][i] = base * 20.0f;
        }
    }

    QString err;
    REQUIRE(writeGdalOutput(t0Path, width, height, t0Bands, gt, crsWkt, &err));
    REQUIRE(writeGdalOutput(t1Path, width, height, t1Bands, gt, crsWkt, &err));
    REQUIRE(writeGdalOutput(demPath, width, height, demBands, gt, crsWkt, &err));

    // ========================================================================
    // Stage 2: Categorical Raster Resampling Protection Verification
    // ========================================================================
    {
        std::vector<std::vector<float>> catBands(1, std::vector<float>(pixelCount, 1.0f));
        const QString catPath = tmpDir.filePath("categorical.tif");
        REQUIRE(writeGdalOutput(catPath, width, height, catBands, gt, crsWkt, &err));

        GDALDatasetH hCat = GDALOpen(catPath.toUtf8().constData(), GA_Update);
        REQUIRE(hCat != nullptr);
        GDALSetMetadataItem(hCat, "CATEGORICAL", "YES", nullptr);
        GDALClose(hCat);

        hCat = GDALOpen(catPath.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(isCategoricalDataset(hCat));

        RSOperatorContext ctx;
        std::string res = "bilinear";
        res = enforceCategoricalResamplingRule(hCat, res, ctx);
        CHECK(res == "nearest");
        GDALClose(hCat);
    }

    // ========================================================================
    // Stage 3: Multi-band MAD Change Detection
    // ========================================================================
    {
        auto cdOp = RSOperatorRegistry::instance().create("rs:change_detection");
        REQUIRE(cdOp != nullptr);

        Json::Value params(Json::objectValue);
        params["before"] = t0Path.toStdString();
        params["after"] = t1Path.toStdString();
        params["output"] = changeOutPath.toStdString();
        params["method"] = "mad";
        params["makeMask"] = true;
        params["threshold"] = 5.0;

        RSOperatorContext ctx;
        Json::Value res = cdOp->run(params, ctx);
        CHECK(QFile::exists(changeOutPath));

        GdalDatasetWrapper cdDs;
        REQUIRE(cdDs.open(changeOutPath));
        std::vector<float> mag(pixelCount);
        REQUIRE(cdDs.readBandData(1, mag.data(), width, height));

        // Verify changed pixels (380..399) have higher change magnitude than background (0..379)
        float maxBg = 0.0f;
        for (int i = 0; i < 380; ++i) {
            if (mag[i] > maxBg) maxBg = mag[i];
        }
        float minChg = 1e9f;
        for (int i = 380; i < 400; ++i) {
            if (mag[i] < minChg) minChg = mag[i];
        }
        CHECK(minChg > maxBg);
    }

    // ========================================================================
    // Stage 4: Post-Classification Sieve / Majority / Recode & Polygonize
    // ========================================================================
    {
        cv::Mat labels(height, width, CV_32S);
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                // Class 1 (water), Class 2 (vegetation), plus isolated noise (Class 3)
                if (r == 5 && c == 5)
                    labels.at<int32_t>(r, c) = 3; // noise pixel
                else if (r < 10)
                    labels.at<int32_t>(r, c) = 1;
                else
                    labels.at<int32_t>(r, c) = 2;
            }
        }

        // Save raw labels
        QVector<QRgb> colorTable = { qRgb(0, 0, 0), qRgb(0, 0, 255), qRgb(0, 255, 0), qRgb(255, 0, 0) };
        REQUIRE(RsPostProcess::saveLabelRaster(labelT0Path, labels, gt.data(), crsWkt, colorTable, {}, -9999.0, &err));

        // Perform sieve filter (remove isolated noise component of area 1)
        cv::Mat sieved;
        REQUIRE(RsPostProcess::sieve(labels, sieved, 2, 8, &err));
        CHECK(sieved.at<int32_t>(5, 5) == 1); // Sieve replaced noise with surrounding majority class 1

        // Perform majority filter
        cv::Mat filtered;
        REQUIRE(RsPostProcess::majorityFilter(sieved, filtered, 3, &err));

        // Save post-processed raster
        REQUIRE(RsPostProcess::saveLabelRaster(postClassPath, filtered, gt.data(), crsWkt, colorTable, {}, -9999.0, &err));
        CHECK(QFile::exists(postClassPath));

        // Polygonize post-processed raster to GeoPackage vector
        REQUIRE(RsPostProcess::polygonize(postClassPath, polyVectorPath, QStringLiteral("class_id"), &err));
        CHECK(QFile::exists(polyVectorPath));
    }
}

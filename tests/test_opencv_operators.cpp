/***************************************************************************
 * test_opencv_operators.cpp  —  Tests for OpenCV-based RSOperators
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/opencv/opencv_filter_operators.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>
#include <cpl_string.h>

#include <QDir>
#include <QTemporaryDir>

#include <atomic>
#include <thread>

using namespace sicnu::operators;
using namespace sicnu::operators::opencv;

namespace {

QString createTestRaster(const QString& dir, const QString& name, int w, int h, int bands = 1) {
    ensureGdalInit();

    QString path = dir + QDir::separator() + name;

    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), w, h, bands, GDT_Float32, opts);
    CSLDestroy(opts);
    REQUIRE(ds != nullptr);

    std::vector<float> data(w * h, 100.0f);
    // Create a simple gradient / edge pattern
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            data[y * w + x] = (x < w / 2) ? 50.0f : 200.0f;
        }
    }

    for (int b = 1; b <= bands; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b);
        CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
        REQUIRE(err == CE_None);
    }

    // Set a simple geotransform and projection
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALSetGeoTransform(ds, gt.data());
    GDALSetProjection(ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");

    GDALClose(ds);
    return path;
}

Json::Value makeParams(const QString& input, const QString& output) {
    Json::Value p(Json::objectValue);
    p["input"] = input.toStdString();
    p["output"] = output.toStdString();
    return p;
}

void verifyOutput(const QString& path, int expectedW, int expectedH, int expectedBands) {
    GdalDatasetWrapper ds;
    REQUIRE(ds.open(path));
    REQUIRE(ds.width() == expectedW);
    REQUIRE(ds.height() == expectedH);
    REQUIRE(ds.bandCount() == expectedBands);
    REQUIRE(!ds.projection().isEmpty());
}

} // anonymous namespace

TEST_CASE("OpenCvGaussianBlurOperator schema and metadata", "[opencv]") {
    auto op = std::make_unique<OpenCvGaussianBlurOperator>();
    REQUIRE(op->name() == "opencv:gaussian_blur");

    Json::Value schema = op->schema();
    REQUIRE(schema["properties"].isMember("input"));
    REQUIRE(schema["properties"].isMember("output"));
    REQUIRE(schema["properties"].isMember("kernelSize"));
    REQUIRE(schema["properties"]["kernelSize"]["default"].asInt() == 5);

    Json::Value meta = op->metadata();
    REQUIRE(meta["group"].asString() == "opencv-filter");
}

TEST_CASE("OpenCvGaussianBlurOperator rejects even kernel size", "[opencv]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "in.tif", 8, 8);
    QString output = tempDir.path() + QDir::separator() + "out.tif";

    auto op = std::make_unique<OpenCvGaussianBlurOperator>();
    RSOperatorContext ctx;
    Json::Value params = makeParams(input, output);
    params["kernelSize"] = 4; // invalid

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::InvalidParameter);
    }
}

TEST_CASE("OpenCvGaussianBlurOperator produces output", "[opencv]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "in.tif", 16, 16);
    QString output = tempDir.path() + QDir::separator() + "gaussian.tif";

    auto op = std::make_unique<OpenCvGaussianBlurOperator>();
    RSOperatorContext ctx;
    Json::Value params = makeParams(input, output);
    params["kernelSize"] = 5;
    params["sigma"] = 1.0;

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    REQUIRE(result["bands"].asInt() == 1);

    verifyOutput(output, 16, 16, 1);
}

TEST_CASE("OpenCvMedianBlurOperator removes impulse noise", "[opencv]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "in.tif", 8, 8);
    QString output = tempDir.path() + QDir::separator() + "median.tif";

    auto op = std::make_unique<OpenCvMedianBlurOperator>();
    RSOperatorContext ctx;
    Json::Value params = makeParams(input, output);
    params["kernelSize"] = 3;

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    verifyOutput(output, 8, 8, 1);
}

TEST_CASE("OpenCvSobelOperator produces output", "[opencv]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "in.tif", 16, 16);
    QString output = tempDir.path() + QDir::separator() + "sobel.tif";

    auto op = std::make_unique<OpenCvSobelOperator>();
    RSOperatorContext ctx;
    Json::Value params = makeParams(input, output);
    params["dx"] = 1;
    params["dy"] = 0;
    params["kernelSize"] = 3;

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    verifyOutput(output, 16, 16, 1);
}

TEST_CASE("OpenCvSobelOperator rejects invalid kernel size", "[opencv]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "in.tif", 8, 8);
    QString output = tempDir.path() + QDir::separator() + "sobel_bad.tif";

    auto op = std::make_unique<OpenCvSobelOperator>();
    RSOperatorContext ctx;
    Json::Value params = makeParams(input, output);
    params["kernelSize"] = 4;

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::InvalidParameter);
    }
}

TEST_CASE("OpenCvLaplacianOperator produces output", "[opencv]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "in.tif", 16, 16);
    QString output = tempDir.path() + QDir::separator() + "laplacian.tif";

    auto op = std::make_unique<OpenCvLaplacianOperator>();
    RSOperatorContext ctx;
    Json::Value params = makeParams(input, output);
    params["kernelSize"] = 3;

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    verifyOutput(output, 16, 16, 1);
}

TEST_CASE("OpenCvCannyOperator produces output", "[opencv]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "in.tif", 16, 16);
    QString output = tempDir.path() + QDir::separator() + "canny.tif";

    auto op = std::make_unique<OpenCvCannyOperator>();
    RSOperatorContext ctx;
    Json::Value params = makeParams(input, output);
    params["threshold1"] = 50.0;
    params["threshold2"] = 150.0;

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    verifyOutput(output, 16, 16, 1);
}

TEST_CASE("OpenCv operators are registered", "[opencv]") {
    RSOperatorRegistry& reg = RSOperatorRegistry::instance();
    REQUIRE(reg.hasOperator("opencv:gaussian_blur"));
    REQUIRE(reg.hasOperator("opencv:median_blur"));
    REQUIRE(reg.hasOperator("opencv:sobel"));
    REQUIRE(reg.hasOperator("opencv:laplacian"));
    REQUIRE(reg.hasOperator("opencv:canny"));

    auto op = reg.create("opencv:gaussian_blur");
    REQUIRE(op != nullptr);
    REQUIRE(op->name() == "opencv:gaussian_blur");
}

TEST_CASE("OpenCv operator cancellation", "[opencv]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    // Larger image to give cancellation time to take effect. 2048^2 x 10
    // bands keeps the read+blur reliably above the 10 ms cancel delay — the
    // old 256^2 raster could finish first once the NoData masking got faster
    // (#444), turning the cancellation contract into a race.
    QString input = createTestRaster(tempDir.path(), "in.tif", 2048, 2048, 10);
    QString output = tempDir.path() + QDir::separator() + "cancelled.tif";

    auto op = std::make_unique<OpenCvGaussianBlurOperator>();
    std::atomic<bool> cancelFlag{false};

    std::thread runner([&]() {
        RSOperatorContext ctx;
        ctx.setCancelFlag(&cancelFlag);
        try {
            op->run(makeParams(input, output), ctx);
            FAIL("Expected cancellation");
        } catch (const RSOperatorError& e) {
            REQUIRE(e.code() == ErrorCode::Cancelled);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cancelFlag.store(true);
    runner.join();
}

TEST_CASE("OpenCvMeanBlurOperator is registered and runs", "[opencv]") {
    REQUIRE(RSOperatorRegistry::instance().hasOperator("opencv:mean_blur"));

    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    QString input = createTestRaster(tempDir.path(), "in.tif", 16, 16);
    QString output = tempDir.path() + QDir::separator() + "mean.tif";

    auto op = std::make_unique<OpenCvMeanBlurOperator>();
    REQUIRE(op->name() == "opencv:mean_blur");
    RSOperatorContext ctx;
    Json::Value params = makeParams(input, output);
    params["kernelSize"] = 3;
    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    verifyOutput(output, 16, 16, 1);
}

// ---------------------------------------------------------------------------
// #444/#445: NoData semantics — large sentinels must match; undeclared NoData
// must not fabricate a sentinel (valid 0 pixels preserved, output never
// declared NoData=0).
// ---------------------------------------------------------------------------
#include "operators/opencv/opencv_utils.h"
#include <opencv2/core.hpp>
#include <QFile>

TEST_CASE("opencv utils mask large-sentinel NoData and keep undeclared zeros (#444/#445)", "[operators][opencv][nodata]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("nd.tif"));
    ensureGdalInit();

    constexpr int W = 4, H = 1;
    const float sentinel = -3.4028235e+38f;
    std::vector<std::vector<float>> bands(1, std::vector<float>{0.5f, sentinel, 0.0f, 0.25f});
    const std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
    QString err;
    REQUIRE(writeGdalOutput(path, W, H, bands, gt, QString(), &err, static_cast<double>(sentinel)));

    // Declared large sentinel: masked to NaN; valid 0 pixel preserved.
    cv::Mat m = sicnu::operators::opencv::readRasterBandToMat(path.toStdString(), 1);
    REQUIRE(m.rows == H);
    REQUIRE(m.cols == W);
    const float *p = m.ptr<float>();
    CHECK(p[0] == 0.5f);
    CHECK(std::isnan(p[1]));
    CHECK(p[2] == 0.0f);
    CHECK(p[3] == 0.25f);

    // Write through writeMatToRaster: source declares sentinel -> output
    // declares it too and NaN pixels are materialized to the sentinel.
    const QString outPath = tmp.filePath(QStringLiteral("nd_out.tif"));
    REQUIRE(sicnu::operators::opencv::writeMatToRaster(outPath.toStdString(), m, path.toStdString()));
    GdalDatasetWrapper out;
    REQUIRE(out.open(outPath));
    bool hasNd = false;
    const double nd = out.bandNoDataValue(1, &hasNd);
    CHECK(hasNd);
    CHECK(static_cast<float>(nd) == sentinel);
    std::vector<float> back(W);
    REQUIRE(out.readBandData(1, back.data(), W, H));
    CHECK(back[1] == sentinel);

    // Undeclared NoData: valid 0 pixels must NOT be masked, and the output
    // must not be declared NoData=0.
    const QString path2 = tmp.filePath(QStringLiteral("nound.tif"));
    std::vector<std::vector<float>> bands2(1, std::vector<float>{0.5f, 1.0f, 0.0f, 0.25f});
    REQUIRE(writeGdalOutput(path2, W, H, bands2, gt, QString(), &err));
    cv::Mat m2 = sicnu::operators::opencv::readRasterBandToMat(path2.toStdString(), 1);
    const float *p2 = m2.ptr<float>();
    CHECK(p2[0] == 0.5f);
    CHECK(p2[2] == 0.0f);   // undeclared -> 0 stays a valid value
    CHECK(!std::isnan(p2[2]));

    const QString outPath2 = tmp.filePath(QStringLiteral("nound_out.tif"));
    REQUIRE(sicnu::operators::opencv::writeMatToRaster(outPath2.toStdString(), m2, path2.toStdString()));
    GdalDatasetWrapper out2;
    REQUIRE(out2.open(outPath2));
    bool hasNd2 = false;
    out2.bandNoDataValue(1, &hasNd2);
    if (hasNd2)
    {
        // If a NoData is declared for an undeclared source it must be NaN, never 0.
        double nd2 = out2.bandNoDataValue(1, &hasNd2);
        CHECK(std::isnan(nd2));
    }
    std::vector<float> back2(W);
    REQUIRE(out2.readBandData(1, back2.data(), W, H));
    CHECK(back2[2] == 0.0f);
}

TEST_CASE("writeMatToRaster does not mutate caller's const cv::Mat containing NaNs (#569)", "[opencv][const_correctness]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString srcPath = tmp.filePath(QStringLiteral("src_nodata.tif"));
    ensureGdalInit();

    constexpr int W = 3, H = 3;
    const float sentinel = -9999.0f;
    std::vector<std::vector<float>> bands(1, std::vector<float>(W * H, 10.0f));
    bands[0][0] = sentinel;
    const std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
    QString err;
    REQUIRE(writeGdalOutput(srcPath, W, H, bands, gt, QString(), &err, static_cast<double>(sentinel)));

    cv::Mat inputMat = sicnu::operators::opencv::readRasterBandToMat(srcPath.toStdString(), 1);
    REQUIRE(inputMat.rows == H);
    REQUIRE(inputMat.cols == W);
    REQUIRE(std::isnan(inputMat.at<float>(0, 0)));

    const QString outPath = tmp.filePath(QStringLiteral("dst_nodata.tif"));
    REQUIRE(sicnu::operators::opencv::writeMatToRaster(outPath.toStdString(), inputMat, srcPath.toStdString()));

    // Caller's inputMat MUST NOT have been mutated: (0, 0) must STILL be NaN!
    CHECK(std::isnan(inputMat.at<float>(0, 0)));

    std::vector<cv::Mat> vecMats = { inputMat };
    const QString outPathVec = tmp.filePath(QStringLiteral("dst_nodata_vec.tif"));
    REQUIRE(sicnu::operators::opencv::writeMatsToRaster(outPathVec.toStdString(), vecMats, srcPath.toStdString()));
    CHECK(std::isnan(inputMat.at<float>(0, 0)));
}

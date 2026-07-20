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

    // Larger image to give cancellation time to take effect
    QString input = createTestRaster(tempDir.path(), "in.tif", 256, 256, 10);
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

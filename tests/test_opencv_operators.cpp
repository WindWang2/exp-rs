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

// ---------------------------------------------------------------------------
// #691: the windowed filters (gaussian/mean/median/sobel/laplacian) run on
// 256x256 halo tiles through GdalBlockStream + GdalStreamingOutput. Results
// must match the full-frame kernel across tile boundaries, and NaN/NoData
// semantics must be preserved at tile and raster edges.
// ---------------------------------------------------------------------------
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {

constexpr int kStreamW = 600; // 3 tiles of 256 + 88-px edge tile
constexpr int kStreamH = 523; // 3 tiles of 256 + 11-px edge tile

/// Multi-band float32 raster with an optional declared NoData; georeferencing
/// matches createTestRaster.
QString createStreamingRaster(const QString& dir, const QString& name, int w, int h,
                              const std::vector<std::vector<float>>& bands,
                              bool declareNodata, double nodata) {
    ensureGdalInit();

    const QString path = dir + QDir::separator() + name;
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);
    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), w, h,
                                 static_cast<int>(bands.size()), GDT_Float32, nullptr);
    REQUIRE(ds != nullptr);

    for (size_t b = 0; b < bands.size(); ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, static_cast<int>(b) + 1);
        if (declareNodata) {
            GDALSetRasterNoDataValue(band, nodata);
        }
        CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, w, h,
                                  const_cast<float*>(bands[b].data()), w, h, GDT_Float32, 0, 0);
        REQUIRE(err == CE_None);
    }

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALSetGeoTransform(ds, gt.data());
    GDALSetProjection(ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");
    GDALClose(ds);
    return path;
}

/// Deterministic multi-frequency pattern (gradients in x and y, plus steps) so
/// every tile sees non-constant input.
std::vector<float> streamingPattern(int w, int h) {
    std::vector<float> data(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            data[static_cast<size_t>(y) * w + x] =
                40.0f + 30.0f * static_cast<float>((x * 7 + y * 13) % 64) / 64.0f
                + 5.0f * static_cast<float>((x / 16) % 2)
                - 4.0f * static_cast<float>((y / 24) % 2);
        }
    }
    return data;
}

/// Reads one band and applies the same masked-read NaN convention as the
/// operators (#444): declared finite sentinels and non-finite values -> NaN.
cv::Mat readBandAsOperatorSeesIt(const QString& path, int w, int h, int bandNum = 1) {
    GdalDatasetWrapper ds;
    REQUIRE(ds.open(path));
    std::vector<float> raw(static_cast<size_t>(w) * h);
    REQUIRE(ds.readBandData(bandNum, raw.data(), w, h));
    bool hasNd = false;
    const double nd = ds.bandNoDataValue(bandNum, &hasNd);

    cv::Mat m(h, w, CV_32FC1);
    std::copy(raw.begin(), raw.end(), m.ptr<float>());
    if (hasNd && std::isfinite(nd)) {
        const float ndF = static_cast<float>(nd);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (size_t i = 0; i < raw.size(); ++i) {
            if (!std::isfinite(m.ptr<float>()[i]) || m.ptr<float>()[i] == ndF)
                m.ptr<float>()[i] = nan;
        }
    }
    return m;
}

/// Full-frame reference of the gaussian/mean mask semantics: normalized
/// convolution (NaN filled from the valid fraction of the window; NaN only
/// where the window has (almost) no valid pixels).
template <typename FilterFn>
cv::Mat normalizedFilterReference(const cv::Mat& src, const FilterFn& filterFn) {
    cv::Mat mask;
    cv::compare(src, src, mask, cv::CMP_EQ); // 255 for non-NaN, 0 for NaN
    const int nonZero = cv::countNonZero(mask);
    if (nonZero == mask.rows * mask.cols) {
        cv::Mat out;
        filterFn(src, out);
        return out;
    }
    if (nonZero == 0) {
        cv::Mat out = src.clone();
        out.setTo(std::numeric_limits<float>::quiet_NaN());
        return out;
    }
    cv::Mat cleanSrc = src.clone();
    cleanSrc.setTo(0.0f, ~mask);
    cv::Mat maskF, filteredData, filteredMask;
    mask.convertTo(maskF, CV_32F, 1.0 / 255.0);
    filterFn(cleanSrc, filteredData);
    filterFn(maskF, filteredMask);
    cv::Mat validMask = (filteredMask > 1e-4f);
    cv::divide(filteredData, filteredMask, filteredData);
    filteredData.setTo(std::numeric_limits<float>::quiet_NaN(), ~validMask);
    return filteredData;
}

/// Full-frame reference of the median/sobel/laplacian mask semantics: the
/// kernel runs on NaN->0 data and every original NaN is echoed to the output.
cv::Mat nanEchoFilterReference(const cv::Mat& src,
                               const std::function<void(const cv::Mat&, cv::Mat&)>& filterFn) {
    cv::Mat mask;
    cv::compare(src, src, mask, cv::CMP_EQ);
    cv::Mat clean = src.clone();
    clean.setTo(0.0f, ~mask);
    cv::Mat out;
    filterFn(clean, out);
    cv::Mat result = out;
    result.setTo(std::numeric_limits<float>::quiet_NaN(), ~mask);
    return result;
}

void requireNear(const cv::Mat& expected, const cv::Mat& actual, double tol) {
    REQUIRE(expected.rows == actual.rows);
    REQUIRE(expected.cols == actual.cols);
    size_t valueMismatches = 0;
    size_t nanMismatches = 0;
    double worstDiff = 0.0;
    for (int y = 0; y < expected.rows; ++y) {
        for (int x = 0; x < expected.cols; ++x) {
            const float e = expected.at<float>(y, x);
            const float a = actual.at<float>(y, x);
            if (std::isnan(e) || std::isnan(a)) {
                if (!(std::isnan(e) && std::isnan(a)))
                    ++nanMismatches;
                continue;
            }
            if (e != a) {
                ++valueMismatches;
                worstDiff = std::max(worstDiff, std::abs(static_cast<double>(e) - a));
            }
        }
    }
    if (nanMismatches != 0 || worstDiff > tol) {
        FAIL("streamed output differs from full-frame kernel: nanMismatches="
             << nanMismatches << " valueMismatches=" << valueMismatches
             << " worstDiff=" << worstDiff);
    }
}

/// Punches sentinel holes into data (corners and raster edges included).
void pokeSentinelHoles(std::vector<float>& data, int w, int h, float sentinel) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if ((x * 31 + y * 17) % 97 == 0)
                data[static_cast<size_t>(y) * w + x] = sentinel;
        }
    }
    data[0] = sentinel;                                // top-left corner
    data[w - 1] = sentinel;                            // top-right corner
    data[static_cast<size_t>(h - 1) * w] = sentinel;   // bottom-left corner
    data[static_cast<size_t>(h - 1) * w + w - 1] = sentinel; // bottom-right
}

} // namespace

TEST_CASE("Streamed gaussian and mean blur match the full-frame kernel across tiles (#691)",
          "[opencv][streaming]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    const auto data = streamingPattern(kStreamW, kStreamH);
    // Two bands with shifted data: bands are processed independently and must
    // each match their own full-frame reference.
    std::vector<float> band2 = streamingPattern(kStreamW, kStreamH);
    std::transform(band2.begin(), band2.end(), band2.begin(), [](float v) { return 200.0f - v; });
    const QString input = createStreamingRaster(tempDir.path(), "in.tif", kStreamW, kStreamH,
                                                {data, band2}, false, 0.0);

    RSOperatorContext ctx;

    SECTION("gaussian k=5 sigma=1.2") {
        const QString output = tempDir.path() + QDir::separator() + "gauss.tif";
        OpenCvGaussianBlurOperator op;
        Json::Value params = makeParams(input, output);
        params["kernelSize"] = 5;
        params["sigma"] = 1.2;
        const Json::Value result = op.run(params, ctx);
        REQUIRE(result["bands"].asInt() == 2);

        const cv::Mat src = readBandAsOperatorSeesIt(input, kStreamW, kStreamH);
        const cv::Mat expected = normalizedFilterReference(src, [](const cv::Mat& in, cv::Mat& out) {
            cv::GaussianBlur(in, out, cv::Size(5, 5), 1.2);
        });
        requireNear(expected, readBandAsOperatorSeesIt(output, kStreamW, kStreamH), 1e-3);

        // Band 2 runs the same per-band path and must match its own reference.
        const cv::Mat src2 = readBandAsOperatorSeesIt(input, kStreamW, kStreamH, 2);
        const cv::Mat expected2 = normalizedFilterReference(src2, [](const cv::Mat& in, cv::Mat& out) {
            cv::GaussianBlur(in, out, cv::Size(5, 5), 1.2);
        });
        requireNear(expected2, readBandAsOperatorSeesIt(output, kStreamW, kStreamH, 2), 1e-3);
    }

    SECTION("mean k=3") {
        const QString output = tempDir.path() + QDir::separator() + "mean.tif";
        OpenCvMeanBlurOperator op;
        Json::Value params = makeParams(input, output);
        params["kernelSize"] = 3;
        op.run(params, ctx);

        const cv::Mat src = readBandAsOperatorSeesIt(input, kStreamW, kStreamH);
        const cv::Mat expected = normalizedFilterReference(src, [](const cv::Mat& in, cv::Mat& out) {
            cv::blur(in, out, cv::Size(3, 3));
        });
        requireNear(expected, readBandAsOperatorSeesIt(output, kStreamW, kStreamH), 1e-3);
    }
}

TEST_CASE("Streamed median blur preserves NaN across tiles and raster edges (#691)",
          "[opencv][streaming][nodata]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    constexpr float sentinel = -9999.0f;
    auto data = streamingPattern(kStreamW, kStreamH);
    pokeSentinelHoles(data, kStreamW, kStreamH, sentinel);
    const QString input = createStreamingRaster(tempDir.path(), "in.tif", kStreamW, kStreamH,
                                                {data}, true, sentinel);

    const QString output = tempDir.path() + QDir::separator() + "median.tif";
    OpenCvMedianBlurOperator op;
    RSOperatorContext ctx;
    Json::Value params = makeParams(input, output);
    params["kernelSize"] = 3;
    op.run(params, ctx);

    const cv::Mat src = readBandAsOperatorSeesIt(input, kStreamW, kStreamH);
    const cv::Mat expected = nanEchoFilterReference(src, [](const cv::Mat& in, cv::Mat& out) {
        cv::medianBlur(in, out, 3);
    });
    const cv::Mat actual = readBandAsOperatorSeesIt(output, kStreamW, kStreamH);
    requireNear(expected, actual, 0.0);

    // NaN echo holds at the punched corners/edges explicitly.
    CHECK(std::isnan(actual.at<float>(0, 0)));
    CHECK(std::isnan(actual.at<float>(0, kStreamW - 1)));
    CHECK(std::isnan(actual.at<float>(kStreamH - 1, 0)));
    CHECK(std::isnan(actual.at<float>(kStreamH - 1, kStreamW - 1)));
}

TEST_CASE("Streamed Sobel and Laplacian echo NaN and match the full-frame kernel (#691)",
          "[opencv][streaming][nodata]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    constexpr float sentinel = -9999.0f;
    auto data = streamingPattern(kStreamW, kStreamH);
    pokeSentinelHoles(data, kStreamW, kStreamH, sentinel);
    const QString input = createStreamingRaster(tempDir.path(), "in.tif", kStreamW, kStreamH,
                                                {data}, true, sentinel);

    const cv::Mat src = readBandAsOperatorSeesIt(input, kStreamW, kStreamH);
    RSOperatorContext ctx;

    SECTION("sobel k=3 dx=1 dy=0") {
        const QString output = tempDir.path() + QDir::separator() + "sobel.tif";
        OpenCvSobelOperator op;
        Json::Value params = makeParams(input, output);
        params["kernelSize"] = 3;
        params["dx"] = 1;
        params["dy"] = 0;
        op.run(params, ctx);

        const cv::Mat expected = nanEchoFilterReference(src, [](const cv::Mat& in, cv::Mat& out) {
            cv::Sobel(in, out, CV_32F, 1, 0, 3);
        });
        requireNear(expected, readBandAsOperatorSeesIt(output, kStreamW, kStreamH), 1e-3);
    }

    SECTION("laplacian k=3") {
        const QString output = tempDir.path() + QDir::separator() + "lap.tif";
        OpenCvLaplacianOperator op;
        Json::Value params = makeParams(input, output);
        params["kernelSize"] = 3;
        op.run(params, ctx);

        const cv::Mat expected = nanEchoFilterReference(src, [](const cv::Mat& in, cv::Mat& out) {
            cv::Laplacian(in, out, CV_32F, 3);
        });
        requireNear(expected, readBandAsOperatorSeesIt(output, kStreamW, kStreamH), 1e-3);
    }
}

TEST_CASE("Streamed filters keep NaN semantics at raster edges (#691)", "[opencv][streaming][nodata]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    // Single-tile raster whose border ring is entirely NoData.
    constexpr int W = 37;
    constexpr int H = 29;
    constexpr float sentinel = -9999.0f;
    std::vector<float> data = streamingPattern(W, H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (x == 0 || y == 0 || x == W - 1 || y == H - 1)
                data[static_cast<size_t>(y) * W + x] = sentinel;
        }
    }
    const QString input = createStreamingRaster(tempDir.path(), "ring.tif", W, H,
                                                {data}, true, sentinel);
    RSOperatorContext ctx;

    SECTION("median echoes the NaN ring, interior stays finite") {
        const QString output = tempDir.path() + QDir::separator() + "median.tif";
        OpenCvMedianBlurOperator op;
        Json::Value params = makeParams(input, output);
        params["kernelSize"] = 3;
        op.run(params, ctx);
        const cv::Mat actual = readBandAsOperatorSeesIt(output, W, H);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const bool ring = (x == 0 || y == 0 || x == W - 1 || y == H - 1);
                if (ring) {
                    CHECK(std::isnan(actual.at<float>(y, x)));
                } else {
                    CHECK(!std::isnan(actual.at<float>(y, x)));
                }
            }
        }
    }

    SECTION("sobel echoes the NaN ring") {
        const QString output = tempDir.path() + QDir::separator() + "sobel.tif";
        OpenCvSobelOperator op;
        Json::Value params = makeParams(input, output);
        params["kernelSize"] = 3;
        params["dx"] = 1;
        params["dy"] = 1;
        op.run(params, ctx);
        const cv::Mat actual = readBandAsOperatorSeesIt(output, W, H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (x == 0 || y == 0 || x == W - 1 || y == H - 1)
                    CHECK(std::isnan(actual.at<float>(y, x)));
    }

    SECTION("gaussian matches the full-frame mask semantics on the NaN ring") {
        const QString output = tempDir.path() + QDir::separator() + "gauss.tif";
        OpenCvGaussianBlurOperator op;
        Json::Value params = makeParams(input, output);
        params["kernelSize"] = 3;
        params["sigma"] = 1.0;
        op.run(params, ctx);
        // Normalized convolution + reflect101 borders: even corner pixels are
        // filled from the valid interior folded back by the border mode, so
        // the exact expectations come from the full-frame reference.
        const cv::Mat src = readBandAsOperatorSeesIt(input, W, H);
        const cv::Mat expected = normalizedFilterReference(src, [](const cv::Mat& in, cv::Mat& out) {
            cv::GaussianBlur(in, out, cv::Size(3, 3), 1.0);
        });
        const cv::Mat actual = readBandAsOperatorSeesIt(output, W, H);
        requireNear(expected, actual, 1e-6);
        // Interior stays finite; every NaN-input pixel is either filled or
        // NaN exactly where the full-frame kernel is NaN (checked above).
        CHECK(!std::isnan(actual.at<float>(H / 2, W / 2)));
    }
}

TEST_CASE("Streamed median keeps undeclared NaN pixels and valid zeros (#691)",
          "[opencv][streaming][nodata]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    // No declared NoData (#444): NaN pixels pass through raw, valid 0 pixels
    // must never be masked.
    constexpr int W = 300;
    constexpr int H = 200;
    auto data = streamingPattern(W, H);
    data[static_cast<size_t>(10) * W + 20] = std::numeric_limits<float>::quiet_NaN();
    data[static_cast<size_t>(150) * W + 250] = std::numeric_limits<float>::quiet_NaN();
    data[static_cast<size_t>(100) * W + 100] = 0.0f;
    const QString input = createStreamingRaster(tempDir.path(), "nan_undeclared.tif", W, H,
                                                {data}, false, 0.0);

    const QString output = tempDir.path() + QDir::separator() + "median_out.tif";
    OpenCvMedianBlurOperator op;
    RSOperatorContext ctx;
    op.run(makeParams(input, output), ctx);

    const cv::Mat src = readBandAsOperatorSeesIt(input, W, H);
    REQUIRE(std::isnan(src.at<float>(10, 20)));
    const cv::Mat expected = nanEchoFilterReference(src, [](const cv::Mat& in, cv::Mat& out) {
        cv::medianBlur(in, out, 3);
    });
    const cv::Mat actual = readBandAsOperatorSeesIt(output, W, H);
    requireNear(expected, actual, 0.0);
    CHECK(std::isnan(actual.at<float>(10, 20)));
    CHECK(std::isnan(actual.at<float>(150, 250)));
    // The valid 0 pixel survives unmasked (#444).
    CHECK(!std::isnan(actual.at<float>(100, 100)));
    CHECK(actual.at<float>(100, 100) == expected.at<float>(100, 100));
}

TEST_CASE("OpenCV windowed filters declare Streaming; Canny stays FullRaster (#691)",
          "[opencv][streaming]") {
    CHECK(OpenCvGaussianBlurOperator().memoryPolicy() == RSOperatorMemoryPolicy::Streaming);
    CHECK(OpenCvMeanBlurOperator().memoryPolicy() == RSOperatorMemoryPolicy::Streaming);
    CHECK(OpenCvMedianBlurOperator().memoryPolicy() == RSOperatorMemoryPolicy::Streaming);
    CHECK(OpenCvSobelOperator().memoryPolicy() == RSOperatorMemoryPolicy::Streaming);
    CHECK(OpenCvLaplacianOperator().memoryPolicy() == RSOperatorMemoryPolicy::Streaming);
    // Canny normalizes with the global band min/max -> genuinely full-frame.
    CHECK(OpenCvCannyOperator().memoryPolicy() == RSOperatorMemoryPolicy::FullRaster);
}

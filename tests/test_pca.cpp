#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include <vector>
#include <cmath>

using namespace Catch;

TEST_CASE("PCA on 2-band correlated data", "[pca]") {
    size_t n = 100;
    size_t bands = 2;
    std::vector<std::vector<float>> input(bands, std::vector<float>(n));
    for (size_t i = 0; i < n; i++) {
        input[0][i] = static_cast<float>(i);
        input[1][i] = 2.0f * i + (i % 3 - 1) * 0.1f;
    }

    auto result = ImageEnhancement::pca(input, 2);

    REQUIRE(result.explainedVariance[0] > 0.99f);
    REQUIRE(result.explainedVariance[1] < 0.01f);
}

TEST_CASE("PCA output dimensions", "[pca]") {
    size_t n = 50;
    size_t bands = 3;
    std::vector<std::vector<float>> input(bands, std::vector<float>(n, 1.0f));

    auto result = ImageEnhancement::pca(input, 2);

    REQUIRE(result.output.size() == 2);
    REQUIRE(result.output[0].size() == n);
    REQUIRE(result.output[1].size() == n);
}

TEST_CASE("PCA variance sums to 1", "[pca]") {
    size_t n = 50;
    std::vector<std::vector<float>> input(3, std::vector<float>(n));
    for (size_t i = 0; i < n; i++) {
        input[0][i] = static_cast<float>(i);
        input[1][i] = static_cast<float>(i * 2);
        input[2][i] = static_cast<float>(i * 3);
    }

    auto result = ImageEnhancement::pca(input, 3);

    float totalVariance = 0;
    for (auto v : result.explainedVariance) totalVariance += v;
    REQUIRE(totalVariance == Approx(1.0f).margin(0.01f));
}

#include "processing/gdal/gdal_dataset_wrapper.h"
#include <QTemporaryDir>
#include <QFile>
#include <gdal.h>

TEST_CASE("ImageEnhancement processPcaFile writes component bands", "[pca][gdal]")
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("source.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

    GDALDatasetH srcDs = createOutputTiff(sourcePath, 2, 2, 3, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);

    for (int b = 1; b <= 3; ++b) {
        std::vector<float> band(4, static_cast<float>(b));
        GDALRasterBandH bandH = GDALGetRasterBand(srcDs, b);
        REQUIRE(GDALRasterIO(bandH, GF_Write, 0, 0, 2, 2, band.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
    }
    GDALClose(srcDs);

    QString error;
    const bool ok = ImageEnhancement::processPcaFile(sourcePath, outputPath, 2, &error);
    REQUIRE(ok);
    REQUIRE(QFile::exists(outputPath));
    REQUIRE(error.isEmpty());

    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(outputPath));
    REQUIRE(outDs.bandCount() == 2);
}

TEST_CASE("PCA skips NaN pixels in the covariance", "[pca][c3]")
{
    // 2 bands, 4 pixels: one NaN pixel must not corrupt the covariance.
    std::vector<std::vector<float>> input(2, std::vector<float>(4, 0.0f));
    input[0] = {1.0f, 2.0f, 3.0f, std::numeric_limits<float>::quiet_NaN()};
    input[1] = {4.0f, 5.0f, 6.0f, std::numeric_limits<float>::quiet_NaN()};

    const ImageEnhancement::PcaResult result = ImageEnhancement::pca(input, 2);
    REQUIRE(result.output.size() == 2);
    // The NaN pixel is not projected; the valid pixels produce finite output.
    CHECK(std::isfinite(result.output[0][0]));
    CHECK(std::isfinite(result.output[1][1]));
    // Explained variance is finite and sums to 1.
    double total = 0.0;
    for (float v : result.explainedVariance)
    {
        CHECK(std::isfinite(v));
        total += v;
    }
    CHECK(total == Catch::Approx(1.0).margin(1e-5));
}

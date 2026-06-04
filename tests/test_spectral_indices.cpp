// tests/test_spectral_indices.cpp — TDD Red phase for spectral index algorithms
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/spectral_indices.h"

#include <vector>
#include <cmath>
#include <limits>

// Helper: create test band data
static std::vector<float> makeBand(const std::vector<float> &values)
{
    return values;
}

// --- NDVI ---

TEST_CASE("NDVI basic calculation", "[spectral][ndvi]")
{
    // NDVI = (NIR - Red) / (NIR + Red)
    std::vector<float> nir = {0.5f, 0.8f, 0.0f, 0.3f};
    std::vector<float> red = {0.1f, 0.2f, 0.0f, 0.3f};
    std::vector<float> out(4);

    bool ok = SpectralIndices::ndvi(nir.data(), red.data(), out.data(), 4);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.4f / 0.6f, 0.001f)); // (0.5-0.1)/(0.5+0.1)
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.6f / 1.0f, 0.001f)); // (0.8-0.2)/(0.8+0.2)
}

TEST_CASE("NDVI handles zero denominator (NIR+Red=0)", "[spectral][ndvi]")
{
    std::vector<float> nir = {0.0f, 0.5f};
    std::vector<float> red = {0.0f, 0.1f};
    std::vector<float> out(2);

    SpectralIndices::ndvi(nir.data(), red.data(), out.data(), 2);
    REQUIRE(std::isnan(out[0])); // 0/0 → NaN
    REQUIRE_FALSE(std::isnan(out[1]));
}

TEST_CASE("NDVI rejects null pointers", "[spectral][ndvi]")
{
    std::vector<float> buf(4);
    REQUIRE_FALSE(SpectralIndices::ndvi(nullptr, buf.data(), buf.data(), 4));
    REQUIRE_FALSE(SpectralIndices::ndvi(buf.data(), nullptr, buf.data(), 4));
    REQUIRE_FALSE(SpectralIndices::ndvi(buf.data(), buf.data(), nullptr, 4));
}

TEST_CASE("NDVI rejects zero size", "[spectral][ndvi]")
{
    std::vector<float> buf(4);
    REQUIRE_FALSE(SpectralIndices::ndvi(buf.data(), buf.data(), buf.data(), 0));
}

// --- EVI ---

TEST_CASE("EVI basic calculation", "[spectral][evi]")
{
    // EVI = 2.5 * (NIR - Red) / (NIR + 6*Red - 7.5*Blue + 1)
    std::vector<float> nir = {0.5f};
    std::vector<float> red = {0.1f};
    std::vector<float> blue = {0.05f};
    std::vector<float> out(1);

    bool ok = SpectralIndices::evi(nir.data(), red.data(), blue.data(), out.data(), 1);
    REQUIRE(ok);
    float expected = 2.5f * (0.5f - 0.1f) / (0.5f + 6.0f * 0.1f - 7.5f * 0.05f + 1.0f);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(expected, 0.001f));
}

TEST_CASE("EVI handles zero denominator", "[spectral][evi]")
{
    // Denominator = NIR + 6*Red - 7.5*Blue + 1
    // With NIR=0, Red=0, Blue=0 → denom=1, so no zero issue
    // But if denom happens to be 0 (unlikely with +1 term), should return NaN
    std::vector<float> nir = {0.0f};
    std::vector<float> red = {0.0f};
    std::vector<float> blue = {0.0f};
    std::vector<float> out(1);

    SpectralIndices::evi(nir.data(), red.data(), blue.data(), out.data(), 1);
    // denom = 0 + 0 - 0 + 1 = 1, so result = 0
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.0f, 0.001f));
}

// --- SAVI ---

TEST_CASE("SAVI basic calculation", "[spectral][savi]")
{
    // SAVI = (NIR - Red) / (NIR + Red + L) * (1 + L), L=0.5
    std::vector<float> nir = {0.5f};
    std::vector<float> red = {0.1f};
    std::vector<float> out(1);

    bool ok = SpectralIndices::savi(nir.data(), red.data(), out.data(), 1);
    REQUIRE(ok);
    float L = 0.5f;
    float expected = (0.5f - 0.1f) / (0.5f + 0.1f + L) * (1.0f + L);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(expected, 0.001f));
}

// --- NDWI ---

TEST_CASE("NDWI basic calculation", "[spectral][ndwi]")
{
    // NDWI = (Green - NIR) / (Green + NIR)
    std::vector<float> green = {0.4f};
    std::vector<float> nir = {0.2f};
    std::vector<float> out(1);

    bool ok = SpectralIndices::ndwi(green.data(), nir.data(), out.data(), 1);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.2f / 0.6f, 0.001f));
}

// --- NDBI ---

TEST_CASE("NDBI basic calculation", "[spectral][ndbi]")
{
    // NDBI = (SWIR - NIR) / (SWIR + NIR)
    std::vector<float> swir = {0.3f};
    std::vector<float> nir = {0.5f};
    std::vector<float> out(1);

    bool ok = SpectralIndices::ndbi(swir.data(), nir.data(), out.data(), 1);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(-0.2f / 0.8f, 0.001f));
}

// --- MNDWI ---

TEST_CASE("MNDWI basic calculation", "[spectral][mndwi]")
{
    // MNDWI = (Green - SWIR) / (Green + SWIR)
    std::vector<float> green = {0.4f};
    std::vector<float> swir = {0.1f};
    std::vector<float> out(1);

    bool ok = SpectralIndices::mndwi(green.data(), swir.data(), out.data(), 1);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.3f / 0.5f, 0.001f));
}

// --- Integration with GdalDatasetWrapper ---

#include "processing/gdal/gdal_dataset_wrapper.h"
#include <QFileInfo>

TEST_CASE("NDVI works with GdalDatasetWrapper band data", "[spectral][ndvi][integration]")
{
    // Use a multi-band raster; for NDVI we need NIR and Red bands
    // phr_xs.tif has 4 bands (Blue, Green, Red, NIR for Pleiades)
    QString base = QFileInfo(__FILE__).absolutePath();
    QString path = QFileInfo(base + "/../data/phr_xs.tif").absoluteFilePath();

    GdalDatasetWrapper ds;
    if (!ds.open(path)) {
        WARN("phr_xs.tif not found, skipping integration test");
        return;
    }

    int w = ds.width();
    int h = ds.height();
    int n = w * h;

    std::vector<float> red(n), nir(n), out(n);
    // Pleiades: Band 1=Blue, 2=Green, 3=Red, 4=NIR
    if (ds.bandCount() < 4) {
        WARN("phr_xs.tif has < 4 bands, skipping");
        return;
    }

    REQUIRE(ds.readBandData(3, red.data(), w, h));
    REQUIRE(ds.readBandData(4, nir.data(), w, h));

    bool ok = SpectralIndices::ndvi(nir.data(), red.data(), out.data(), n);
    REQUIRE(ok);

    // Verify output is in valid range [-1, 1] for non-NaN pixels
    int validCount = 0;
    for (int i = 0; i < n; i++) {
        if (!std::isnan(out[i])) {
            REQUIRE(out[i] >= -1.0f);
            REQUIRE(out[i] <= 1.0f);
            validCount++;
        }
    }
    REQUIRE(validCount > 0);
}

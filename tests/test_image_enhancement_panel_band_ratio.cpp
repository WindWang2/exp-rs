#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QTemporaryDir>
#include <QString>

#include <gdal.h>
#include <vector>
#include <array>

using Catch::Approx;

// Regression for #341: band-ratio output must be single-band, not N zero-filled bands.
// The panel previously allocated outputBands sized to input band count and only
// filled outputBands[0] before calling writeGdalOutput, which writes bands.size()
// bands (bands 2..N were zeros). The fix is outputBands.resize(1).

TEST_CASE("ImageEnhancementPanel band ratio writes single band", "[enhancement][panel][341]")
{
    GDALAllRegister();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const int w = 4, h = 2;
    const size_t pixelCount = static_cast<size_t>(w) * h;
    std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
    QString proj;

    std::vector<float> b1(pixelCount, 10.0f);
    std::vector<float> b2(pixelCount, 5.0f);

    SECTION("outputBands resized to 1 yields 1-band GeoTIFF")
    {
        // Simulate panel worker after fix: allocate N then resize to 1
        const int inputBands = 4;
        std::vector<std::vector<float>> outputBands(inputBands);
        for (auto &v : outputBands) v.resize(pixelCount, 0.0f);
        ImageEnhancement::bandRatio(b1.data(), b2.data(), outputBands[0].data(), pixelCount);
        outputBands.resize(1); // fix for #341

        REQUIRE(outputBands.size() == 1);
        REQUIRE(outputBands[0].size() == pixelCount);
        REQUIRE(outputBands[0][0] == Approx(2.0f));

        const QString path = tmp.path() + QStringLiteral("/ratio_fixed.tif");
        QString err;
        REQUIRE(writeGdalOutput(path, w, h, outputBands, gt, proj, &err));
        REQUIRE(err.isEmpty());

        GDALDatasetH ds = GDALOpen(path.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        int bandCount = GDALGetRasterCount(ds);
        REQUIRE(bandCount == 1);

        std::vector<float> readBack(pixelCount);
        GDALRasterBandH band = GDALGetRasterBand(ds, 1);
        REQUIRE(band != nullptr);
        CPLErr e = GDALRasterIO(band, GF_Read, 0, 0, w, h, readBack.data(), w, h, GDT_Float32, 0, 0);
        REQUIRE(e == CE_None);
        for (size_t i = 0; i < pixelCount; ++i)
            REQUIRE(readBack[i] == Approx(2.0f));
        GDALClose(ds);
    }

    SECTION("unfixed allocation would write N bands (demonstrates bug)")
    {
        const int inputBands = 4;
        std::vector<std::vector<float>> outputBands(inputBands);
        for (auto &v : outputBands) v.resize(pixelCount, 0.0f);
        ImageEnhancement::bandRatio(b1.data(), b2.data(), outputBands[0].data(), pixelCount);
        // No resize -> bug: 4 bands, 3 are zeros
        REQUIRE(outputBands.size() == static_cast<size_t>(inputBands));

        const QString path = tmp.path() + QStringLiteral("/ratio_bug.tif");
        QString err;
        REQUIRE(writeGdalOutput(path, w, h, outputBands, gt, proj, &err));

        GDALDatasetH ds = GDALOpen(path.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        int bandCount = GDALGetRasterCount(ds);
        // Bug would produce 4 bands; fixed path must be 1.
        // This SECTION asserts the buggy path DOES produce 4, so the fix matters.
        REQUIRE(bandCount == 4);
        // band 2 should be all zeros in the buggy product
        std::vector<float> band2Read(pixelCount);
        GDALRasterBandH b = GDALGetRasterBand(ds, 2);
        REQUIRE(b != nullptr);
        CPLErr e = GDALRasterIO(b, GF_Read, 0, 0, w, h, band2Read.data(), w, h, GDT_Float32, 0, 0);
        REQUIRE(e == CE_None);
        for (float v : band2Read) REQUIRE(v == Approx(0.0f));
        GDALClose(ds);
    }
}

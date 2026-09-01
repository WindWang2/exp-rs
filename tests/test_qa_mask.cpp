// test_qa_mask.cpp — QA / cloud / shadow / snow mask kernels and operator
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include <json/json.h>

#include <gdal_priv.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/algorithms/qa_mask.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;

namespace {

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_qa_mask";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

/// Writes a multi-band Float32 raster with a geotransform and stamps the given
/// band's SICNU_BAND_ROLE metadata (mirrors a product-stacked output).
void writeRasterWithRole(const QString& path, const std::vector<std::vector<float>>& bands,
                         int roleBand, const char* roleId)
{
    ensureGdalInit();
    const int width = 4;
    const int height = static_cast<int>(bands.empty() ? 0 : bands[0].size() / width);
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(path, width, height, bands, gt, "EPSG:32648", &err));

    if (roleBand > 0) {
        GDALDatasetH ds = GDALOpen(path.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        REQUIRE(GDALGetRasterBand(ds, roleBand) != nullptr);
        GDALSetMetadataItem(GDALGetRasterBand(ds, roleBand), "SICNU_BAND_ROLE", roleId, nullptr);
        GDALClose(ds);
    }
}

} // namespace

// --- Kernel tests ----------------------------------------------------------

TEST_CASE("QA mask kernels", "[qa]")
{
    SECTION("Landsat QA_PIXEL bits") {
        const uint16_t qa[] = {0, 8, 16, 32, 1, 24};
        uint8_t mask[6] = {};
        // cloud + dilated + cirrus + shadow (no snow/fill/water)
        QaMask::landsatQaMask(qa, mask, 6,
                              QaMask::LandsatMaskCloud | QaMask::LandsatMaskDilatedCloud
                                  | QaMask::LandsatMaskCirrus | QaMask::LandsatMaskCloudShadow);
        CHECK(mask[0] == 0); // clear
        CHECK(mask[1] == 1); // cloud (bit 3)
        CHECK(mask[2] == 1); // cloud shadow (bit 4)
        CHECK(mask[3] == 0); // snow only
        CHECK(mask[4] == 0); // fill only
        CHECK(mask[5] == 1); // cloud 8 + shadow 16
    }
    SECTION("Sentinel-2 SCL classes") {
        const uint8_t scl[] = {0, 3, 8, 9, 10, 11, 4, 255};
        bool classes[16] = {};
        classes[QaMask::SclCloudShadow] = true;
        classes[QaMask::SclCloudMediumProbability] = true;
        classes[QaMask::SclCloudHighProbability] = true;
        classes[QaMask::SclThinCirrus] = true;
        uint8_t mask[8] = {};
        QaMask::sclMask(scl, mask, 8, classes);
        CHECK(mask[0] == 0); // no data
        CHECK(mask[1] == 1); // cloud shadow
        CHECK(mask[2] == 1); // medium cloud
        CHECK(mask[3] == 1); // high cloud
        CHECK(mask[4] == 1); // thin cirrus
        CHECK(mask[5] == 0); // snow not selected
        CHECK(mask[6] == 0); // vegetation
        CHECK(mask[7] == 0); // out-of-range class guarded

        // Null pointer safety checks
        QaMask::sclMask(nullptr, mask, 8, classes);
        QaMask::sclMask(scl, nullptr, 8, classes);
        QaMask::sclMask(scl, mask, 8, nullptr);
    }
    SECTION("Generic bitmask") {
        const uint16_t values[] = {0, 1, 2, 3, 4};
        uint8_t mask[5] = {};
        QaMask::genericBitmaskMask(values, mask, 5, 0x3);
        CHECK(mask[0] == 0);
        CHECK(mask[1] == 1); // 1 & 3
        CHECK(mask[2] == 1); // 2 & 3
        CHECK(mask[3] == 1); // 3 & 3
        CHECK(mask[4] == 0); // 4 & 3 = 0
    }
}

// --- Operator tests --------------------------------------------------------

TEST_CASE("rs:qa_mask masks Landsat QA_PIXEL via role-resolved band", "[operators][rs][qa]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString input = tmp.path() + "/landsat_stack.tif";
    const QString output = tmp.path() + "/cloud_mask.tif";

    // 2 bands: band 1 = reflectance (ignored), band 2 = QA_PIXEL (role qa).
    constexpr int W = 4;
    constexpr int H = 1;
    std::vector<std::vector<float>> bands(2, std::vector<float>(W * H, 0.0f));
    bands[1][0] = 8.0f;   // cloud (bit 3)
    bands[1][1] = 16.0f;  // cloud shadow (bit 4)
    bands[1][2] = 32.0f;  // snow (bit 5) — not in cloud_and_shadow
    bands[1][3] = 1.0f;   // fill (bit 0) — not in cloud_and_shadow
    writeRasterWithRole(input, bands, 2, "qa");

    auto op = RSOperatorRegistry::instance().create("rs:qa_mask");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["mask"] = "cloud_and_shadow";

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["output"].asString() == output.toStdString());
    CHECK(result["source"].asString() == "landsat_qa_pixel");
    CHECK(result["maskClasses"].asString() == "cloud_and_shadow");
    CHECK(result["maskedPixels"].asUInt64() == 2);
    CHECK(result["totalPixels"].asUInt64() == 4);
    CHECK(result["maskedPercent"].asDouble() == Catch::Approx(50.0));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(output));
    std::vector<float> mask(W * H);
    REQUIRE(ds.readBandData(1, mask.data(), W, H));
    CHECK(mask[0] == 1.0f);
    CHECK(mask[1] == 1.0f);
    CHECK(mask[2] == 0.0f);
    CHECK(mask[3] == 0.0f);
}

TEST_CASE("rs:qa_mask masks Sentinel-2 SCL via role-resolved band", "[operators][rs][qa]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString input = tmp.path() + "/scl.tif";
    const QString output = tmp.path() + "/scl_mask.tif";

    constexpr int W = 4;
    constexpr int H = 1;
    std::vector<std::vector<float>> bands(1, std::vector<float>(W * H, 0.0f));
    bands[0][0] = 8.0f;   // cloud medium
    bands[0][1] = 3.0f;   // cloud shadow
    bands[0][2] = 11.0f;  // snow
    bands[0][3] = 4.0f;   // vegetation
    writeRasterWithRole(input, bands, 1, "scene_classification");

    auto op = RSOperatorRegistry::instance().create("rs:qa_mask");
    REQUIRE(op != nullptr);

    SECTION("Auto source resolves to sentinel2_scl; snow selection") {
        Json::Value params(Json::objectValue);
        params["input"] = input.toStdString();
        params["output"] = output.toStdString();
        params["mask"] = "snow";

        RSOperatorContext ctx;
        Json::Value result = op->run(params, ctx);
        CHECK(result["source"].asString() == "sentinel2_scl");
        CHECK(result["maskedPixels"].asUInt64() == 1);

        GdalDatasetWrapper ds;
        REQUIRE(ds.open(output));
        std::vector<float> mask(W * H);
        REQUIRE(ds.readBandData(1, mask.data(), W, H));
        CHECK(mask[0] == 0.0f);
        CHECK(mask[1] == 0.0f);
        CHECK(mask[2] == 1.0f); // snow only
        CHECK(mask[3] == 0.0f);
    }
    SECTION("cloud_and_shadow masks cloud + shadow") {
        Json::Value params(Json::objectValue);
        params["input"] = input.toStdString();
        params["output"] = output.toStdString();

        RSOperatorContext ctx;
        Json::Value result = op->run(params, ctx);
        CHECK(result["source"].asString() == "sentinel2_scl");
        CHECK(result["maskedPixels"].asUInt64() == 2);
    }
}

TEST_CASE("rs:qa_mask supports explicit qa_band and generic bitmask", "[operators][rs][qa]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString input = tmp.path() + "/generic.tif";
    const QString output = tmp.path() + "/generic_mask.tif";

    // 3 bands, no roles; band 3 carries the bit values.
    constexpr int W = 4;
    constexpr int H = 1;
    std::vector<std::vector<float>> bands(3, std::vector<float>(W * H, 0.0f));
    bands[2][0] = 1.0f;
    bands[2][1] = 2.0f;
    bands[2][2] = 3.0f;
    bands[2][3] = 4.0f;
    writeRasterWithRole(input, bands, 0, nullptr);

    auto op = RSOperatorRegistry::instance().create("rs:qa_mask");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["qa_band"] = 3;
    params["source"] = "generic_bitmask";
    params["bits"] = 0x3;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["source"].asString() == "generic_bitmask");
    CHECK(result["maskedPixels"].asUInt64() == 3);

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(output));
    std::vector<float> mask(W * H);
    REQUIRE(ds.readBandData(1, mask.data(), W, H));
    CHECK(mask[0] == 1.0f);
    CHECK(mask[1] == 1.0f);
    CHECK(mask[2] == 1.0f);
    CHECK(mask[3] == 0.0f);
}

TEST_CASE("rs:qa_mask rejects rasters without a QA band", "[operators][rs][qa]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString input = tmp.path() + "/plain.tif";
    const QString output = tmp.path() + "/out.tif";

    std::vector<std::vector<float>> bands(1, std::vector<float>(4, 1.0f));
    writeRasterWithRole(input, bands, 0, nullptr);

    auto op = RSOperatorRegistry::instance().create("rs:qa_mask");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();

    RSOperatorContext ctx;
    REQUIRE_THROWS_AS(op->run(params, ctx), RSOperatorError);
    CHECK_FALSE(QFile::exists(output));
}

// --- #699: native-type QA reads (no float round-trip + UB casts) -----------

TEST_CASE("rs:qa_mask reads float QA bands natively and guards non-finite values (#699)",
          "[operators][rs][qa]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString input = tmp.path() + "/qa_float.tif";
    const QString output = tmp.path() + "/qa_float_mask.tif";

    // Float32 QA band mixing valid flags with values the old float->uint16
    // cast turned into UB or silent truncation.
    ensureGdalInit();
    constexpr int W = 4, H = 1;
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    GDALDatasetH ds = createOutputTiff(input, W, H, 1, GDT_Float32, gt, QString());
    REQUIRE(ds != nullptr);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> qa = {8.0f, nan, 70000.0f, -5.0f};
    REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Write, 0, 0, W, H, qa.data(),
                         W, H, GDT_Float32, 0, 0) == CE_None);
    GDALClose(ds);

    auto op = RSOperatorRegistry::instance().create("rs:qa_mask");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["qa_band"] = 1;
    params["source"] = "landsat_qa_pixel";
    params["mask"] = "cloud_and_shadow";

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    // 8 = cloud -> masked; NaN -> clear (no UB); 70000 -> clamped to 65535
    // (all QA bits set -> masked), not truncated; -5 -> clear.
    CHECK(result["maskedPixels"].asUInt64() == 2);
    CHECK(result["totalPixels"].asUInt64() == 4);

    GdalDatasetWrapper out;
    REQUIRE(out.open(output));
    std::vector<float> mask(W * H);
    REQUIRE(out.readBandData(1, mask.data(), W, H));
    CHECK(mask[0] == 1.0f);
    CHECK(mask[1] == 0.0f);
    CHECK(mask[2] == 1.0f);
    CHECK(mask[3] == 0.0f);
}

TEST_CASE("rs:qa_mask reads UInt16 QA bands natively and honours declared nodata (#699)",
          "[operators][rs][qa]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString input = tmp.path() + "/qa_uint16.tif";
    const QString output = tmp.path() + "/qa_uint16_mask.tif";

    // Native UInt16 QA band (real Landsat C2 QA_PIXEL dtype) with declared
    // nodata 65535 — previously read through Float32 and cast back.
    ensureGdalInit();
    constexpr int W = 4, H = 1;
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    GDALDatasetH ds = createOutputTiff(input, W, H, 1, GDT_UInt16, gt, QString());
    REQUIRE(ds != nullptr);
    const uint16_t qa[W] = {8, 16, 65535, 4};
    REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Write, 0, 0, W, H,
                         const_cast<uint16_t*>(qa), W, H, GDT_UInt16, 0, 0) == CE_None);
    REQUIRE(GDALSetRasterNoDataValue(GDALGetRasterBand(ds, 1), 65535.0) == CE_None);
    GDALClose(ds);

    auto op = RSOperatorRegistry::instance().create("rs:qa_mask");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["qa_band"] = 1;
    params["source"] = "landsat_qa_pixel";
    params["mask"] = "cloud_and_shadow";

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    // 8 = cloud -> masked; 16 = shadow -> masked; 65535 = declared nodata ->
    // unmasked; 4 -> clear. The declared-nodata pixel must not be confused
    // with the "all flags set" QA word 65535.
    CHECK(result["maskedPixels"].asUInt64() == 2);
    CHECK(result["totalPixels"].asUInt64() == 4);

    GdalDatasetWrapper out;
    REQUIRE(out.open(output));
    std::vector<float> mask(W * H);
    REQUIRE(out.readBandData(1, mask.data(), W, H));
    CHECK(mask[0] == 1.0f);
    CHECK(mask[1] == 1.0f);
    CHECK(mask[2] == 0.0f);
    CHECK(mask[3] == 0.0f);
}

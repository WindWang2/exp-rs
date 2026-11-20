// tests/test_r35_t01_landsat_affine.cpp — R35-T01 WP01
//
// Landsat Collection-2 L2 band identity: real C2L2 MTL files key their bands
// as FILE_NAME_BAND_SR_Bn / FILE_NAME_BAND_ST_B10, and the MTL discovery path
// keeps those names. The stack's reflectance-affine stamping must bind
// REFLECTANCE_MULT/ADD_BAND_n through the SR_ prefix (the directory-scan
// fallback strips it; the MTL path does not), while ST/QA bands stay out of
// the reflectance contract.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <gdal.h>

#include "processing/algorithms/satellite_products.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_r35_t01_landsat_affine";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

void writeTinyBand(const QString &path, float fill)
{
    ensureGdalInit();
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    std::vector<std::vector<float>> bands(1, std::vector<float>(4 * 4, fill));
    QString err;
    REQUIRE(writeGdalOutput(path, 4, 4, bands, gt, QStringLiteral("EPSG:32648"), &err));
}

/// Minimal C2L2 scene: MTL with SR_B2/SR_B3/ST_B10 band keys (the identity a
/// real Collection-2 L2 MTL carries) plus per-band reflectance coefficients.
struct C2L2Scene
{
    QString mtlPath;
    float srB2Fill = 100.f;
    float srB3Fill = 120.f;
};

C2L2Scene writeC2L2Scene(const QDir &root, const QStringList &extraMtlLines = {},
                         bool uniformCoefficients = true)
{
    C2L2Scene scene;
    const QString sceneDir = root.filePath(QStringLiteral("LC08_L2SP_TEST"));
    QDir().mkpath(sceneDir);
    QDir sdir(sceneDir);

    QFile mtl(sdir.filePath(QStringLiteral("LC08_L2SP_TEST_MTL.txt")));
    REQUIRE(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&mtl);
    out << "GROUP = LANDSAT_METADATA_FILE\n";
    out << "  SPACECRAFT_ID = \"LANDSAT_8\"\n";
    out << "  PROCESSING_LEVEL = \"L2SP\"\n";
    out << "  DATE_ACQUIRED = \"2020-06-15\"\n";
    out << "  LANDSAT_PRODUCT_ID = \"LC08_L2SP_TEST\"\n";
    out << "  FILE_NAME_BAND_SR_B2 = \"LC08_L2SP_TEST_SR_B2.TIF\"\n";
    out << "  FILE_NAME_BAND_SR_B3 = \"LC08_L2SP_TEST_SR_B3.TIF\"\n";
    out << "  FILE_NAME_BAND_ST_B10 = \"LC08_L2SP_TEST_ST_B10.TIF\"\n";
    out << "  REFLECTANCE_MULT_BAND_2 = \"0.0000275\"\n";
    out << "  REFLECTANCE_ADD_BAND_2 = \"-0.2\"\n";
    out << "  REFLECTANCE_MULT_BAND_3 = \""
        << (uniformCoefficients ? QStringLiteral("0.0000275") : QStringLiteral("0.0000135"))
        << "\"\n";
    out << "  REFLECTANCE_ADD_BAND_3 = \"-0.2\"\n";
    for (const QString &line : extraMtlLines)
        out << "  " << line << "\n";
    out << "END_GROUP = LANDSAT_METADATA_FILE\n";
    out << "END\n";
    mtl.close();

    writeTinyBand(sdir.filePath(QStringLiteral("LC08_L2SP_TEST_SR_B2.TIF")), scene.srB2Fill);
    writeTinyBand(sdir.filePath(QStringLiteral("LC08_L2SP_TEST_SR_B3.TIF")), scene.srB3Fill);
    writeTinyBand(sdir.filePath(QStringLiteral("LC08_L2SP_TEST_ST_B10.TIF")), 290.f);
    scene.mtlPath = sdir.filePath(QStringLiteral("LC08_L2SP_TEST_MTL.txt"));
    return scene;
}

const char *bandMetadata(GDALDatasetH ds, int band, const char *key)
{
    GDALRasterBandH bandH = GDALGetRasterBand(ds, band);
    REQUIRE(bandH != nullptr);
    return GDALGetMetadataItem(bandH, key, nullptr);
}

} // namespace

TEST_CASE("C2L2 SR band keys bind MTL reflectance coefficients (WP01)",
          "[r35t01][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const C2L2Scene scene = writeC2L2Scene(QDir(tmp.path()));
    const QString out = tmp.filePath(QStringLiteral("sr_stack.tif"));

    SatelliteProducts::ProductInfo info;
    REQUIRE(SatelliteProducts::discoverLandsat(scene.mtlPath, &info));
    // The MTL-key path keeps the SR_ identity — the fix must work with it.
    QStringList names;
    for (const auto &b : info.bands)
        names << b.name;
    REQUIRE(names.contains(QStringLiteral("SR_B2")));
    REQUIRE(names.contains(QStringLiteral("SR_B3")));
    REQUIRE(names.contains(QStringLiteral("ST_B10")));

    QString err;
    REQUIRE(SatelliteProducts::stackToGeoTiff(
        info, {QStringLiteral("SR_B2"), QStringLiteral("SR_B3")}, out, &err));

    GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);

    const char *state = GDALGetMetadataItem(ds, "SICNU_RADIOMETRIC_STATE", nullptr);
    REQUIRE(state != nullptr);
    CHECK(QString::fromUtf8(state) == QStringLiteral("surface_reflectance"));

    // Uniform MULT/ADD over all selected bands: the dataset-wide contract.
    const double mult = 0.0000275;
    const double add = -0.2;
    const char *scale = GDALGetMetadataItem(ds, "SICNU_NUMERIC_SCALE", nullptr);
    REQUIRE(scale != nullptr);
    CHECK(QString::fromUtf8(scale) == QString::number(1.0 / mult, 'g', 12));
    const char *offset = GDALGetMetadataItem(ds, "SICNU_NUMERIC_OFFSET", nullptr);
    REQUIRE(offset != nullptr);
    CHECK(QString::fromUtf8(offset) == QString::number(-add / mult, 'g', 12));

    // Per-band stamps on both SR bands.
    for (int b = 1; b <= 2; ++b)
    {
        INFO("band " << b);
        REQUIRE(bandMetadata(ds, b, "SICNU_NUMERIC_SCALE") != nullptr);
        REQUIRE(bandMetadata(ds, b, "SICNU_NUMERIC_OFFSET") != nullptr);
    }

    // The stamped contract recovers true reflectance from the stored DN:
    // (stored - offset) / scale == stored * mult + add (no double scaling).
    const double stored = 100.0;
    const double stampedScale = QString::fromUtf8(scale).toDouble();
    const double stampedOffset = QString::fromUtf8(offset).toDouble();
    const double recovered = (stored - stampedOffset) / stampedScale;
    REQUIRE(std::abs(recovered - (stored * mult + add)) < 1e-9);

    // Pixels stay verbatim DN.
    std::vector<float> pixels(16);
    REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 4, 4, pixels.data(), 4, 4,
                         GDT_Float32, 0, 0) == CE_None);
    for (float v : pixels)
        REQUIRE(v == 100.0f);
    GDALClose(ds);
}

TEST_CASE("Mixed SR+ST C2L2 stacks stamp the affine contract per band only (WP01)",
          "[r35t01][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const C2L2Scene scene = writeC2L2Scene(QDir(tmp.path()));
    const QString out = tmp.filePath(QStringLiteral("sr_st_stack.tif"));

    SatelliteProducts::ProductInfo info;
    REQUIRE(SatelliteProducts::discoverLandsat(scene.mtlPath, &info));
    QString err;
    REQUIRE(SatelliteProducts::stackToGeoTiff(
        info, {QStringLiteral("SR_B3"), QStringLiteral("ST_B10")}, out, &err));

    GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);
    REQUIRE(GDALGetRasterCount(ds) == 2);

    // The SR band carries the reflectance affine; the thermal band does not.
    REQUIRE(bandMetadata(ds, 1, "SICNU_NUMERIC_SCALE") != nullptr);
    CHECK(bandMetadata(ds, 2, "SICNU_NUMERIC_SCALE") == nullptr);

    // Heterogeneous contract: the dataset-wide stamp is not honest and is
    // withheld.
    CHECK(GDALGetMetadataItem(ds, "SICNU_NUMERIC_SCALE", nullptr) == nullptr);
    GDALClose(ds);
}

TEST_CASE("Non-uniform C2L2 coefficients stamp nothing instead of a fake scale (WP01)",
          "[r35t01][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const C2L2Scene scene = writeC2L2Scene(QDir(tmp.path()), {}, /*uniformCoefficients=*/false);
    const QString out = tmp.filePath(QStringLiteral("sr_mixed_stack.tif"));

    SatelliteProducts::ProductInfo info;
    REQUIRE(SatelliteProducts::discoverLandsat(scene.mtlPath, &info));
    QString err;
    REQUIRE(SatelliteProducts::stackToGeoTiff(
        info, {QStringLiteral("SR_B2"), QStringLiteral("SR_B3")}, out, &err));

    GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);
    CHECK(GDALGetMetadataItem(ds, "SICNU_NUMERIC_SCALE", nullptr) == nullptr);
    CHECK(bandMetadata(ds, 1, "SICNU_NUMERIC_SCALE") == nullptr);
    CHECK(bandMetadata(ds, 2, "SICNU_NUMERIC_SCALE") == nullptr);
    GDALClose(ds);
}

TEST_CASE("Permuted C2L2 band selection keeps name-keyed coefficients and order (WP01)",
          "[r35t01][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const C2L2Scene scene = writeC2L2Scene(QDir(tmp.path()));
    const QString out = tmp.filePath(QStringLiteral("sr_permuted_stack.tif"));

    SatelliteProducts::ProductInfo info;
    REQUIRE(SatelliteProducts::discoverLandsat(scene.mtlPath, &info));
    QString err;
    // SR_B3 first: the stamp must follow the band name, not the slot order.
    REQUIRE(SatelliteProducts::stackToGeoTiff(
        info, {QStringLiteral("SR_B3"), QStringLiteral("SR_B2")}, out, &err));

    GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);

    const char *scale = GDALGetMetadataItem(ds, "SICNU_NUMERIC_SCALE", nullptr);
    REQUIRE(scale != nullptr);
    CHECK(QString::fromUtf8(scale) == QString::number(1.0 / 0.0000275, 'g', 12));

    // Band order follows the requested selection: B3 fill lands in band 1.
    std::vector<float> p1(16), p2(16);
    REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 4, 4, p1.data(), 4, 4,
                         GDT_Float32, 0, 0) == CE_None);
    REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, 4, 4, p2.data(), 4, 4,
                         GDT_Float32, 0, 0) == CE_None);
    REQUIRE(p1[0] == 120.0f);
    REQUIRE(p2[0] == 100.0f);
    GDALClose(ds);
}

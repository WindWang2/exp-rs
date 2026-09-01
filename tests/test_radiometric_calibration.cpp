// tests/test_radiometric_calibration.cpp - radiometric calibration TDD tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/radiometric_calibration.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace RadiometricCalibration;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Kernel: toRadiance
// ---------------------------------------------------------------------------

TEST_CASE("toRadiance converts DN to radiance", "[radcal]")
{
    // L = gain * DN + bias
    std::vector<float> dn = {100.0f, 200.0f, 50.0f};
    std::vector<float> out(3);
    BandCoefficients c;
    c.radianceGain = 0.01;
    c.radianceBias = -0.1;

    REQUIRE(toRadiance(dn.data(), out.data(), 3, c));
    REQUIRE_THAT(out[0], WithinAbs(0.9f, 0.001f));   // 0.01*100 - 0.1
    REQUIRE_THAT(out[1], WithinAbs(1.9f, 0.001f));   // 0.01*200 - 0.1
    REQUIRE_THAT(out[2], WithinAbs(0.4f, 0.001f));   // 0.01*50  - 0.1
}

TEST_CASE("toRadiance rejects null/zero", "[radcal]")
{
    std::vector<float> buf(3);
    BandCoefficients c;
    REQUIRE_FALSE(toRadiance(nullptr, buf.data(), 3, c));
    REQUIRE_FALSE(toRadiance(buf.data(), nullptr, 3, c));
    REQUIRE_FALSE(toRadiance(buf.data(), buf.data(), 0, c));
}

// ---------------------------------------------------------------------------
// Kernel: toToaReflectance (Landsat-style)
// ---------------------------------------------------------------------------

TEST_CASE("toToaReflectance Landsat formula divides by sin(sunEl)", "[radcal]")
{
    // rho = (reflMult * DN + reflAdd) / sin(sunElevation)
    // sunElevation=30deg -> sin=0.5
    std::vector<float> dn = {100.0f, 200.0f};
    std::vector<float> out(2);
    BandCoefficients c;
    c.reflMult = 0.00002;
    c.reflAdd = -0.1;
    const double sunEl = 30.0;

    REQUIRE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Landsat, sunEl));
    // (0.00002*100 - 0.1) / 0.5 = (-0.098)/0.5 = -0.196
    REQUIRE_THAT(out[0], WithinAbs(-0.196f, 0.001f));
    // (0.00002*200 - 0.1) / 0.5 = (-0.096)/0.5 = -0.192
    REQUIRE_THAT(out[1], WithinAbs(-0.192f, 0.001f));
}

TEST_CASE("toToaReflectance rejects invalid sun elevation", "[radcal]")
{
    std::vector<float> dn = {100.0f};
    std::vector<float> out(1);
    BandCoefficients c;
    c.reflMult = 0.00002;
    c.reflAdd = 0.0;
    // sun elevation 0 -> sin=0 -> invalid
    REQUIRE_FALSE(toToaReflectance(dn.data(), out.data(), 1, c, SensorType::Landsat, 0.0));
    // negative
    REQUIRE_FALSE(toToaReflectance(dn.data(), out.data(), 1, c, SensorType::Landsat, -10.0));
}

TEST_CASE("toToaReflectance Landsat rejects missing reflMult/reflAdd", "[radcal]")
{
    // Landsat with default coefficients (no MTL reflectance values) -> should fail
    std::vector<float> dn = {100.0f};
    std::vector<float> out(1);
    BandCoefficients c;  // defaults: reflMult=1.0, reflAdd=0.0
    REQUIRE_FALSE(toToaReflectance(dn.data(), out.data(), 1, c, SensorType::Landsat, 45.0));
}

// ---------------------------------------------------------------------------
// Kernel: toToaReflectance (Sentinel-2 / generic scale-offset)
// ---------------------------------------------------------------------------

TEST_CASE("toToaReflectance Sentinel-2 scale-offset formula", "[radcal]")
{
    // rho = (DN + offset) / scale
    std::vector<float> dn = {1000.0f, 5000.0f};
    std::vector<float> out(2);
    BandCoefficients c;
    c.scale = 10000.0;
    c.offset = 0.0;
    REQUIRE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Sentinel2, 90.0));
    REQUIRE_THAT(out[0], WithinAbs(0.1f, 0.001f));   // 1000/10000
    REQUIRE_THAT(out[1], WithinAbs(0.5f, 0.001f));   // 5000/10000
}

TEST_CASE("toToaReflectance Sentinel-2 rejects zero scale", "[radcal]")
{
    std::vector<float> dn = {100.0f};
    std::vector<float> out(1);
    BandCoefficients c;
    c.scale = 0.0;
    REQUIRE_FALSE(toToaReflectance(dn.data(), out.data(), 1, c, SensorType::Sentinel2, 90.0));
}

// ---------------------------------------------------------------------------
// Kernel: toBrightnessTemperature
// ---------------------------------------------------------------------------

TEST_CASE("toBrightnessTemperature inverts Planck with K1/K2", "[radcal]")
{
    // L = gain*DN + bias; T = K2 / ln(K1/L + 1)
    std::vector<float> dn = {100.0f};
    std::vector<float> out(1);
    BandCoefficients c;
    c.radianceGain = 0.1;   // L = 10.0
    c.radianceBias = 0.0;
    c.k1 = 607.76;          // Landsat 8 TIRS Band 10
    c.k2 = 1260.56;

    REQUIRE(toBrightnessTemperature(dn.data(), out.data(), 1, c));
    // T = 1260.56 / ln(607.76/10 + 1) = 1260.56 / ln(61.776) = 1260.56 / 4.1236 = 305.6 K
    REQUIRE_THAT(out[0], WithinAbs(305.6f, 0.5f));
}

TEST_CASE("toBrightnessTemperature rejects zero K1/K2", "[radcal]")
{
    std::vector<float> dn = {100.0f};
    std::vector<float> out(1);
    BandCoefficients c;
    c.k1 = 0.0;
    c.k2 = 0.0;
    REQUIRE_FALSE(toBrightnessTemperature(dn.data(), out.data(), 1, c));
}

// ---------------------------------------------------------------------------
// Metadata: Landsat MTL parsing
// ---------------------------------------------------------------------------

namespace {

void writeMtlFile(const QString &path, const QStringList &lines)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream s(&f);
    s << "GROUP = L1_METADATA_FILE\n";
    for (const QString &l : lines)
        s << l << "\n";
    s << "END_GROUP = L1_METADATA_FILE\n";
    s << "END\n";
}

} // namespace

TEST_CASE("parseMtlKeyValues reads flat key/value map", "[radcal][mtl]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));
    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("SUN_ELEVATION = 45.5"),
        QStringLiteral("RADIANCE_MULT_BAND_4 = 0.0123"),
        QStringLiteral("RADIANCE_ADD_BAND_4 = -0.123"),
        QStringLiteral("REFLECTANCE_MULT_BAND_4 = 0.00002"),
        QStringLiteral("REFLECTANCE_ADD_BAND_4 = -0.1"),
    });

    QString err;
    const auto kv = SatelliteProducts::parseMtlKeyValues(mtlPath, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(kv.value(QStringLiteral("SPACECRAFT_ID")) == QStringLiteral("LANDSAT_8"));
    REQUIRE(kv.value(QStringLiteral("SUN_ELEVATION")) == QStringLiteral("45.5"));
    REQUIRE(kv.value(QStringLiteral("RADIANCE_MULT_BAND_4")) == QStringLiteral("0.0123"));
}

TEST_CASE("loadMetadata reads Landsat MTL coefficients", "[radcal][mtl]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));
    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("PROCESSING_LEVEL = L1TP"),
        QStringLiteral("DATE_ACQUIRED = 2024-01-15"),
        QStringLiteral("SUN_ELEVATION = 45.0"),
        QStringLiteral("RADIANCE_MULT_BAND_4 = 0.0123"),
        QStringLiteral("RADIANCE_ADD_BAND_4 = -0.123"),
        QStringLiteral("REFLECTANCE_MULT_BAND_4 = 0.00002"),
        QStringLiteral("REFLECTANCE_ADD_BAND_4 = -0.1"),
        QStringLiteral("K1_CONSTANT_BAND_10 = 607.76"),
        QStringLiteral("K2_CONSTANT_BAND_10 = 1260.56"),
    });

    // Simulate a stacked raster where band 1 = B4, band 2 = B10.
    QMap<int, QString> bandNames;
    bandNames.insert(1, QStringLiteral("B4"));
    bandNames.insert(2, QStringLiteral("B10"));

    CalibrationMetadata meta;
    QString err;
    REQUIRE(loadMetadata(QString(), mtlPath, bandNames, &meta, &err));
    REQUIRE(meta.sensor == SensorType::Landsat);
    REQUIRE_THAT(meta.sunElevationDeg, WithinAbs(45.0, 0.001));
    REQUIRE(meta.spacecraft == QStringLiteral("LANDSAT_8"));

    // Band 1 (B4) -> radiance gain/bias + reflectance
    REQUIRE(meta.bands.contains(1));
    const auto &c4 = meta.bands.value(1);
    REQUIRE_THAT(c4.radianceGain, WithinAbs(0.0123, 0.0001));
    REQUIRE_THAT(c4.radianceBias, WithinAbs(-0.123, 0.001));
    REQUIRE_THAT(c4.reflMult, WithinAbs(0.00002, 0.000001));
    REQUIRE_THAT(c4.reflAdd, WithinAbs(-0.1, 0.001));

    // Band 2 (B10) -> thermal constants
    REQUIRE(meta.bands.contains(2));
    const auto &c10 = meta.bands.value(2);
    REQUIRE_THAT(c10.k1, WithinAbs(607.76, 0.01));
    REQUIRE_THAT(c10.k2, WithinAbs(1260.56, 0.01));
}

// ---------------------------------------------------------------------------
// Metadata: Sentinel-2 MTD XML parsing
// ---------------------------------------------------------------------------

TEST_CASE("loadMetadata reads Sentinel-2 L2A MTD", "[radcal][mtd]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString mtdPath = dir.filePath(QStringLiteral("MTD_MSIL2A.xml"));
    {
        QFile f(mtdPath);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream s(&f);
        s << QStringLiteral(
        "<n1:Level-2A_User_Product xmlns:n1=\"https://das.gsfc.nasa.gov\">\n"
        "  <General_Info>\n"
        "    <Product_Image_Characteristics>\n"
        "      <Radiometric_Info>\n"
        "        <BOA_ADD_OFFSET>\n"
        "          <BOA_LIST_TO_VALUES>\n"
        "            <BOA_LIST_VALUE band_id=\"0\">-1000</BOA_LIST_VALUE>\n"
        "            <BOA_LIST_VALUE band_id=\"1\">-1000</BOA_LIST_VALUE>\n"
        "          </BOA_LIST_TO_VALUES>\n"
        "        </BOA_ADD_OFFSET>\n"
        "        <BOA_QUANTIFICATION_VALUE>10000</BOA_QUANTIFICATION_VALUE>\n"
        "      </Radiometric_Info>\n"
        "    </Product_Image_Characteristics>\n"
        "  </General_Info>\n"
        "  <Geometric_Info>\n"
        "    <Sun_Angles>\n"
        "      <Mean_Sun_Zenith_Angle>30.0</Mean_Sun_Zenith_Angle>\n"
        "    </Sun_Angles>\n"
        "  </Geometric_Info>\n"
        "</n1:Level-2A_User_Product>\n");
        s.flush();
        f.close();
    }

    QMap<int, QString> bandNames;
    bandNames.insert(1, QStringLiteral("B2"));  // S2 band_id 1
    bandNames.insert(2, QStringLiteral("B3"));  // S2 band_id 2

    CalibrationMetadata meta;
    QString err;
    const bool loaded = loadMetadata(QString(), mtdPath, bandNames, &meta, &err);
    INFO("loadMetadata error: " << err.toStdString());
    REQUIRE(loaded);
    REQUIRE(meta.sensor == SensorType::Sentinel2);
    REQUIRE(meta.processingLevel == QStringLiteral("L2A"));
    REQUIRE_THAT(meta.sunElevationDeg, WithinAbs(60.0, 0.001));  // 90 - 30

    // B2 -> band_id 1, offset -1000, scale 10000
    REQUIRE(meta.bands.contains(1));
    REQUIRE_THAT(meta.bands.value(1).offset, WithinAbs(-1000.0, 0.1));
    REQUIRE_THAT(meta.bands.value(1).scale, WithinAbs(10000.0, 0.1));
}

TEST_CASE("loadMetadata reads Sentinel-2 L1C MTD", "[radcal][mtd]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString mtdPath = dir.filePath(QStringLiteral("MTD_MSIL1C.xml"));
    {
        QFile f(mtdPath);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream s(&f);
        // L1C uses RADIO_ADD_OFFSET / RADIO_QUANTIFICATION_VALUE and
        // RADIO_LIST_TO_VALUES / RADIO_LIST_VALUE inner tags.
        s << QStringLiteral(
            "<n1:Level-1C_User_Product xmlns:n1=\"https://das.gsfc.nasa.gov\">\n"
            "  <General_Info>\n"
            "    <Product_Image_Characteristics>\n"
            "      <Radiometric_Info>\n"
            "        <RADIO_ADD_OFFSET>\n"
            "          <RADIO_LIST_TO_VALUES>\n"
            "            <RADIO_LIST_VALUE band_id=\"1\">-500</RADIO_LIST_VALUE>\n"
            "            <RADIO_LIST_VALUE band_id=\"2\">-500</RADIO_LIST_VALUE>\n"
            "          </RADIO_LIST_TO_VALUES>\n"
            "        </RADIO_ADD_OFFSET>\n"
            "        <RADIO_QUANTIFICATION_VALUE>12000</RADIO_QUANTIFICATION_VALUE>\n"
            "      </Radiometric_Info>\n"
            "    </Product_Image_Characteristics>\n"
            "  </General_Info>\n"
            "  <Geometric_Info>\n"
            "    <Sun_Angles>\n"
            "      <ZENITH_ANGLE>25.0</ZENITH_ANGLE>\n"
            "    </Sun_Angles>\n"
            "  </Geometric_Info>\n"
            "</n1:Level-1C_User_Product>\n");
        s.flush();
        f.close();
    }

    QMap<int, QString> bandNames;
    bandNames.insert(1, QStringLiteral("B2"));  // S2 band_id 1
    bandNames.insert(2, QStringLiteral("B3"));  // S2 band_id 2

    CalibrationMetadata meta;
    QString err;
    const bool loaded = loadMetadata(QString(), mtdPath, bandNames, &meta, &err);
    INFO("loadMetadata error: " << err.toStdString());
    REQUIRE(loaded);
    REQUIRE(meta.sensor == SensorType::Sentinel2);
    REQUIRE(meta.processingLevel == QStringLiteral("L1C"));
    REQUIRE_THAT(meta.sunElevationDeg, WithinAbs(65.0, 0.001));  // 90 - 25

    // B2 -> band_id 1, offset -500, scale 12000
    REQUIRE(meta.bands.contains(1));
    REQUIRE_THAT(meta.bands.value(1).offset, WithinAbs(-500.0, 0.1));
    REQUIRE_THAT(meta.bands.value(1).scale, WithinAbs(12000.0, 0.1));
}

// ---------------------------------------------------------------------------
// File-level integration
// ---------------------------------------------------------------------------

TEST_CASE("processFile converts DN to radiance via MTL", "[radcal][gdal]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("source.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));

    // 2x2 single-band raster, DN = [100, 200, 50, 80]
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(sourcePath, 2, 2, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<float> band = {100.0f, 200.0f, 50.0f, 80.0f};
    GDALRasterBandH b1 = GDALGetRasterBand(srcDs, 1);
    REQUIRE(GDALRasterIO(b1, GF_Write, 0, 0, 2, 2, band.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
    GDALSetDescription(b1, "B4");
    GDALClose(srcDs);

    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("SUN_ELEVATION = 45.0"),
        QStringLiteral("RADIANCE_MULT_BAND_4 = 0.01"),
        QStringLiteral("RADIANCE_ADD_BAND_4 = 0.0"),
    });

    QString error;
    const bool ok = processFile(sourcePath, outputPath, mtlPath,
                                static_cast<int>(OutputUnit::Radiance),
                                {}, &error);
    REQUIRE(ok);
    REQUIRE(error.isEmpty());
    REQUIRE(QFile::exists(outputPath));

    // Read back: L = 0.01 * DN
    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    REQUIRE(out.bandCount() == 1);
    std::vector<float> result(4);
    REQUIRE(out.readBandData(1, result.data(), 2, 2));
    REQUIRE_THAT(result[0], WithinAbs(1.0f, 0.001f));   // 0.01*100
    REQUIRE_THAT(result[1], WithinAbs(2.0f, 0.001f));   // 0.01*200
    REQUIRE_THAT(result[2], WithinAbs(0.5f, 0.001f));   // 0.01*50
    REQUIRE_THAT(result[3], WithinAbs(0.8f, 0.001f));   // 0.01*80
}

TEST_CASE("processFile works without band descriptions (identity mapping)", "[radcal][gdal]")
{
    // When the raster has no band descriptions, calibration should fall back to
    // identity mapping (raster band i == MTL band i).
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("source.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(sourcePath, 2, 1, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<float> band = {100.0f, 200.0f};
    GDALRasterBandH b1 = GDALGetRasterBand(srcDs, 1);
    REQUIRE(GDALRasterIO(b1, GF_Write, 0, 0, 2, 1, band.data(), 2, 1, GDT_Float32, 0, 0) == CE_None);
    // No GDALSetDescription - band has no description.
    GDALClose(srcDs);

    // MTL uses band 1 coefficients (identity: raster band 1 == MTL band 1).
    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("SUN_ELEVATION = 45.0"),
        QStringLiteral("RADIANCE_MULT_BAND_1 = 0.01"),
        QStringLiteral("RADIANCE_ADD_BAND_1 = 0.0"),
    });

    QString error;
    const bool ok = processFile(sourcePath, outputPath, mtlPath,
                                static_cast<int>(OutputUnit::Radiance),
                                {}, &error);
    INFO("processFile error: " << error.toStdString());
    REQUIRE(ok);
    REQUIRE(error.isEmpty());

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    std::vector<float> result(2);
    REQUIRE(out.readBandData(1, result.data(), 2, 1));
    REQUIRE_THAT(result[0], WithinAbs(1.0f, 0.001f));   // 0.01*100
    REQUIRE_THAT(result[1], WithinAbs(2.0f, 0.001f));   // 0.01*200
}

TEST_CASE("processFile TOA reflectance via MTL", "[radcal][gdal]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("source.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(sourcePath, 2, 1, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<float> band = {10000.0f, 20000.0f};
    GDALRasterBandH b1 = GDALGetRasterBand(srcDs, 1);
    REQUIRE(GDALRasterIO(b1, GF_Write, 0, 0, 2, 1, band.data(), 2, 1, GDT_Float32, 0, 0) == CE_None);
    GDALSetDescription(b1, "B4");
    GDALClose(srcDs);

    // reflMult=0.00002, reflAdd=0, sunEl=90 (sin=1)
    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("SUN_ELEVATION = 90.0"),
        QStringLiteral("REFLECTANCE_MULT_BAND_4 = 0.00002"),
        QStringLiteral("REFLECTANCE_ADD_BAND_4 = 0.0"),
    });

    QString error;
    const bool ok = processFile(sourcePath, outputPath, mtlPath,
                                static_cast<int>(OutputUnit::ToaReflectance),
                                {}, &error);
    REQUIRE(ok);
    REQUIRE(error.isEmpty());

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    std::vector<float> result(2);
    REQUIRE(out.readBandData(1, result.data(), 2, 1));
    // rho = 0.00002 * DN / sin(90) = 0.00002*10000 = 0.2, 0.00002*20000 = 0.4
    REQUIRE_THAT(result[0], WithinAbs(0.2f, 0.001f));
    REQUIRE_THAT(result[1], WithinAbs(0.4f, 0.001f));
}

// ---------------------------------------------------------------------------
// Out-of-core streaming (tile-by-tile) path
// ---------------------------------------------------------------------------

TEST_CASE("GdalDatasetWrapper writeBandWindow round-trip", "[radcal][gdal][stream]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("wins.tif"));

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GdalDatasetWrapper out;
    REQUIRE(out.create(path, 300, 200, 1, GDT_Float32, gt, QString()));

    // Two 256x200 windows: left 256 columns, right 44 columns.
    std::vector<float> left(256 * 200, 1.5f);
    std::vector<float> right(44 * 200, 2.5f);
    REQUIRE(out.writeBandWindow(1, 0, 0, 256, 200, left.data()));
    REQUIRE(out.writeBandWindow(1, 256, 0, 44, 200, right.data()));
    out.close();

    GdalDatasetWrapper in;
    REQUIRE(in.open(path));
    std::vector<float> row(300);
    REQUIRE(in.readBandWindow(1, 0, 0, 300, 1, row.data()));
    for (int x = 0; x < 300; ++x)
        REQUIRE_THAT(row[static_cast<size_t>(x)], WithinAbs(x < 256 ? 1.5f : 2.5f, 1e-4f));
}

TEST_CASE("processFile streams a multi-tile raster to radiance", "[radcal][gdal][stream]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const int W = 300, H = 200; // spans a 2x1 tile grid at 256px tiles
    const QString sourcePath = dir.filePath(QStringLiteral("source.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(sourcePath, W, H, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<float> band(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            band[static_cast<size_t>(y) * W + x] = static_cast<float>((x + 3 * y) % 256);
    REQUIRE(GDALRasterIO(GDALGetRasterBand(srcDs, 1), GF_Write, 0, 0, W, H,
                         band.data(), W, H, GDT_Float32, 0, 0) == CE_None);
    GDALSetDescription(GDALGetRasterBand(srcDs, 1), "B4");
    GDALClose(srcDs);

    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("SUN_ELEVATION = 45.0"),
        QStringLiteral("RADIANCE_MULT_BAND_4 = 0.01"),
        QStringLiteral("RADIANCE_ADD_BAND_4 = 0.0"),
    });

    QString error;
    REQUIRE(processFile(sourcePath, outputPath, mtlPath,
                        static_cast<int>(OutputUnit::Radiance), {}, &error));
    REQUIRE(error.isEmpty());

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    REQUIRE(out.width() == W);
    REQUIRE(out.height() == H);
    std::vector<float> result(static_cast<size_t>(W) * H);
    REQUIRE(out.readBandData(1, result.data(), W, H));
    for (size_t i = 0; i < band.size(); ++i)
        REQUIRE_THAT(result[i], WithinAbs(0.01f * band[i], 1e-3f));
}

TEST_CASE("processFile streams a band subset", "[radcal][gdal][stream]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const int W = 260, H = 260; // 2x2 tile grid
    const QString sourcePath = dir.filePath(QStringLiteral("source.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(sourcePath, W, H, 3, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    const char *names[3] = {"B2", "B3", "B4"};
    for (int b = 0; b < 3; ++b) {
        std::vector<float> band(static_cast<size_t>(W) * H, 100.0f * (b + 1));
        REQUIRE(GDALRasterIO(GDALGetRasterBand(srcDs, b + 1), GF_Write, 0, 0, W, H,
                             band.data(), W, H, GDT_Float32, 0, 0) == CE_None);
        GDALSetDescription(GDALGetRasterBand(srcDs, b + 1), names[b]);
    }
    GDALClose(srcDs);

    // Coefficients for B2 and B4 only; B3 deliberately absent.
    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("SUN_ELEVATION = 45.0"),
        QStringLiteral("RADIANCE_MULT_BAND_2 = 0.02"),
        QStringLiteral("RADIANCE_ADD_BAND_2 = 0.1"),
        QStringLiteral("RADIANCE_MULT_BAND_4 = 0.04"),
        QStringLiteral("RADIANCE_ADD_BAND_4 = 0.0"),
    });

    QString error;
    REQUIRE(processFile(sourcePath, outputPath, mtlPath,
                        static_cast<int>(OutputUnit::Radiance), {1, 3}, &error));
    REQUIRE(error.isEmpty());

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    REQUIRE(out.bandCount() == 2);
    std::vector<float> b2(static_cast<size_t>(W) * H), b4(static_cast<size_t>(W) * H);
    REQUIRE(out.readBandData(1, b2.data(), W, H));
    REQUIRE(out.readBandData(2, b4.data(), W, H));
    for (size_t i = 0; i < b2.size(); ++i) {
        REQUIRE_THAT(b2[i], WithinAbs(0.02f * 100.0f + 0.1f, 1e-3f)); // B2: 2.1
        REQUIRE_THAT(b4[i], WithinAbs(0.04f * 300.0f, 1e-3f));        // B4: 12.0
    }
}

// ---------------------------------------------------------------------------
// Metadata auto-detection (sibling MTL/MTD scan)
// ---------------------------------------------------------------------------

TEST_CASE("autoDetectMetadataFile scans sibling MTL/MTD files", "[radcal][metadata]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    SECTION("No metadata files next to the raster") {
        CHECK(RadiometricCalibration::autoDetectMetadataFile(
                  dir.filePath(QStringLiteral("scene.tif")))
                  .isEmpty());
    }

    SECTION("A sibling Landsat MTL is detected") {
        writeMtlFile(dir.filePath(QStringLiteral("LC08_L1TP_MTL.txt")), {});
        const QString found = RadiometricCalibration::autoDetectMetadataFile(
            dir.filePath(QStringLiteral("scene.tif")));
        REQUIRE_FALSE(found.isEmpty());
        CHECK(found.endsWith(QStringLiteral("LC08_L1TP_MTL.txt")));
    }

    SECTION("A sibling Sentinel-2 MTD is detected") {
        QFile mtd(dir.filePath(QStringLiteral("MTD_MSIL2A.xml")));
        REQUIRE(mtd.open(QIODevice::WriteOnly | QIODevice::Text));
        mtd.write("<n1:Level-2A_User_Product></n1:Level-2A_User_Product>");
        mtd.close();

        const QString found = RadiometricCalibration::autoDetectMetadataFile(
            dir.filePath(QStringLiteral("scene.tif")));
        REQUIRE_FALSE(found.isEmpty());
        CHECK(found.endsWith(QStringLiteral("MTD_MSIL2A.xml")));
    }

    SECTION("Both present: MTL wins by default, product type overrides") {
        const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));
        writeMtlFile(mtlPath, {});
        QFile mtd(dir.filePath(QStringLiteral("MTD_MSIL2A.xml")));
        REQUIRE(mtd.open(QIODevice::WriteOnly | QIODevice::Text));
        mtd.write("<n1:Level-2A_User_Product></n1:Level-2A_User_Product>");
        mtd.close();

        // Without embedded product type: MTL.
        CHECK(RadiometricCalibration::autoDetectMetadataFile(
                  dir.filePath(QStringLiteral("scene.tif")))
                  .endsWith(QStringLiteral("LC08_MTL.txt")));

        // With SICNU_PRODUCT_TYPE=Sentinel-2 (as written by stackToGeoTiff): MTD.
        const QString stacked = dir.filePath(QStringLiteral("s2_stack.tif"));
        std::vector<std::vector<float>> bands(1, std::vector<float>(4, 1.0f));
        std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
        QString err;
        REQUIRE(writeGdalOutput(stacked, 2, 2, bands, gt, QString(), &err));
        GDALDatasetH ds = GDALOpen(stacked.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        GDALSetMetadataItem(ds, "SICNU_PRODUCT_TYPE", "Sentinel-2", nullptr);
        GDALClose(ds);

        CHECK(RadiometricCalibration::autoDetectMetadataFile(stacked)
                  .endsWith(QStringLiteral("MTD_MSIL2A.xml")));
    }

    SECTION("Multiple MTL files fail closed naming the candidates (#699)") {
        writeMtlFile(dir.filePath(QStringLiteral("LC08_A_MTL.txt")), {});
        writeMtlFile(dir.filePath(QStringLiteral("LC08_B_MTL.txt")), {});
        QString err;
        const QString found = RadiometricCalibration::autoDetectMetadataFile(
            dir.filePath(QStringLiteral("scene.tif")), &err);
        // No alphabetical guessing: refuse to pick and name the candidates.
        CHECK(found.isEmpty());
        REQUIRE_FALSE(err.isEmpty());
        CHECK(err.contains(QStringLiteral("LC08_A_MTL.txt")));
        CHECK(err.contains(QStringLiteral("LC08_B_MTL.txt")));
        CHECK(err.contains(QStringLiteral("metadata_path")));
    }

    SECTION("Multiple MTD files fail closed naming the candidates (#699)") {
        for (const QString& name : {QStringLiteral("MTD_MSIL1C.xml"),
                                    QStringLiteral("MTD_MSIL2A.xml")}) {
            QFile mtd(dir.filePath(name));
            REQUIRE(mtd.open(QIODevice::WriteOnly | QIODevice::Text));
            mtd.write("<n1:Level-2A_User_Product></n1:Level-2A_User_Product>");
            mtd.close();
        }
        QString err;
        const QString found = RadiometricCalibration::autoDetectMetadataFile(
            dir.filePath(QStringLiteral("scene.tif")), &err);
        CHECK(found.isEmpty());
        REQUIRE_FALSE(err.isEmpty());
        CHECK(err.contains(QStringLiteral("MTD_MSIL1C.xml")));
        CHECK(err.contains(QStringLiteral("MTD_MSIL2A.xml")));
    }
}

TEST_CASE("toToaReflectance generic GDAL scale/offset computes DN*scale + offset", "[radcal][toa][generic]")
{
    using namespace RadiometricCalibration;
    const std::vector<float> dn = {0.0f, 1000.0f, 5000.0f};
    std::vector<float> refl(dn.size());

    BandCoefficients c;
    c.scale = 0.0001; // MODIS-style scale factor
    c.offset = -0.05;

    REQUIRE(toToaReflectance(dn.data(), refl.data(), dn.size(), c, SensorType::Generic, 90.0));
    CHECK_THAT(refl[0], Catch::Matchers::WithinAbs(-0.05f, 1e-5f));
    CHECK_THAT(refl[1], Catch::Matchers::WithinAbs(0.05f, 1e-5f));
    CHECK_THAT(refl[2], Catch::Matchers::WithinAbs(0.45f, 1e-5f));
}

TEST_CASE("Calibration kernels reject non-finite or near-zero coefficients", "[radcal][validation]")
{
    using namespace RadiometricCalibration;
    std::vector<float> dn = {100.0f, 200.0f};
    std::vector<float> out(2);

    SECTION("toRadiance rejects non-finite gain and bias") {
        BandCoefficients c;
        c.hasRadiance = true;
        c.radianceGain = std::numeric_limits<double>::quiet_NaN();
        c.radianceBias = 0.0;
        CHECK_FALSE(toRadiance(dn.data(), out.data(), 2, c));

        c.radianceGain = std::numeric_limits<double>::infinity();
        CHECK_FALSE(toRadiance(dn.data(), out.data(), 2, c));

        c.radianceGain = 0.01;
        c.radianceBias = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(toRadiance(dn.data(), out.data(), 2, c));
    }

    SECTION("toToaReflectance Landsat rejects non-finite coefficients") {
        BandCoefficients c;
        c.reflMult = std::numeric_limits<double>::quiet_NaN();
        c.reflAdd = 0.0;
        CHECK_FALSE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Landsat, 45.0));

        c.reflMult = 0.00002;
        c.reflAdd = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Landsat, 45.0));

        c.reflAdd = -0.1;
        CHECK_FALSE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Landsat, std::numeric_limits<double>::quiet_NaN()));
    }

    SECTION("toToaReflectance Sentinel-2 rejects zero or near-zero or non-finite scale") {
        BandCoefficients c;
        c.scale = 0.0;
        c.offset = 0.0;
        CHECK_FALSE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Sentinel2, 90.0));

        c.scale = 1e-15; // below 1e-12 threshold
        CHECK_FALSE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Sentinel2, 90.0));

        c.scale = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Sentinel2, 90.0));

        c.scale = 10000.0;
        c.offset = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Sentinel2, 90.0));
    }

    SECTION("toBrightnessTemperature rejects non-finite coefficients") {
        BandCoefficients c;
        c.k1 = 774.8853;
        c.k2 = 1321.0789;
        c.radianceGain = std::numeric_limits<double>::quiet_NaN();
        c.radianceBias = 0.1;
        CHECK_FALSE(toBrightnessTemperature(dn.data(), out.data(), 2, c));

        c.radianceGain = 0.0003342;
        c.k1 = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(toBrightnessTemperature(dn.data(), out.data(), 2, c));

        c.k1 = 774.8853;
        c.k2 = std::numeric_limits<double>::infinity();
        CHECK_FALSE(toBrightnessTemperature(dn.data(), out.data(), 2, c));
    }
}


// ---------------------------------------------------------------------------
// #435: empty bandNames must auto-discover bands from the metadata itself
// ---------------------------------------------------------------------------

TEST_CASE("loadMetadata with empty bandNames auto-discovers Landsat MTL bands (#435)", "[radcal][mtl][435]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));
    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("SUN_ELEVATION = 45.0"),
        QStringLiteral("RADIANCE_MULT_BAND_4 = 0.0123"),
        QStringLiteral("RADIANCE_ADD_BAND_4 = -0.123"),
        QStringLiteral("REFLECTANCE_MULT_BAND_4 = 0.00002"),
        QStringLiteral("REFLECTANCE_ADD_BAND_4 = -0.1"),
        QStringLiteral("K1_CONSTANT_BAND_ST_B10 = 607.76"),
        QStringLiteral("K2_CONSTANT_BAND_ST_B10 = 1260.56"),
    });

    // Documented contract: empty map -> coefficients keyed by band number.
    QMap<int, QString> bandNames;
    CalibrationMetadata meta;
    QString err;
    const bool loaded = loadMetadata(QString(), mtlPath, bandNames, &meta, &err);
    INFO("loadMetadata error: " << err.toStdString());
    REQUIRE(loaded);

    // Numeric band key (4) discovered from RADIANCE_*_BAND_4
    REQUIRE(meta.bands.contains(4));
    REQUIRE_THAT(meta.bands.value(4).radianceGain, WithinAbs(0.0123, 0.0001));
    REQUIRE_THAT(meta.bands.value(4).radianceBias, WithinAbs(-0.123, 0.001));
    REQUIRE_THAT(meta.bands.value(4).reflMult, WithinAbs(0.00002, 0.000001));

    // Collection-2 ST_B10 token discovered via K1/K2 keys, keyed by band 10
    REQUIRE(meta.bands.contains(10));
    REQUIRE_THAT(meta.bands.value(10).k1, WithinAbs(607.76, 0.01));
    REQUIRE_THAT(meta.bands.value(10).k2, WithinAbs(1260.56, 0.01));
}

TEST_CASE("loadMetadata with empty bandNames auto-discovers Sentinel-2 MTD bands (#435)", "[radcal][mtd][435]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString mtdPath = dir.filePath(QStringLiteral("MTD_MSIL2A.xml"));
    {
        QFile f(mtdPath);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream s(&f);
        s << QStringLiteral(
        "<n1:Level-2A_User_Product xmlns:n1=\"https://das.gsfc.nasa.gov\">\n"
        "  <General_Info>\n"
        "    <Product_Image_Characteristics>\n"
        "      <Radiometric_Info>\n"
        "        <BOA_ADD_OFFSET>\n"
        "          <BOA_LIST_TO_VALUES>\n"
        "            <BOA_LIST_VALUE band_id=\"1\">-1000</BOA_LIST_VALUE>\n"
        "          </BOA_LIST_TO_VALUES>\n"
        "        </BOA_ADD_OFFSET>\n"
        "        <BOA_QUANTIFICATION_VALUE>10000</BOA_QUANTIFICATION_VALUE>\n"
        "      </Radiometric_Info>\n"
        "    </Product_Image_Characteristics>\n"
        "  </General_Info>\n"
        "</n1:Level-2A_User_Product>\n");
        s.flush();
        f.close();
    }

    QMap<int, QString> bandNames;
    CalibrationMetadata meta;
    QString err;
    const bool loaded = loadMetadata(QString(), mtdPath, bandNames, &meta, &err);
    INFO("loadMetadata error: " << err.toStdString());
    REQUIRE(loaded);

    // band_id 1 (B2) -> 1-based band number 2 with offset + quantification scale
    REQUIRE(meta.bands.contains(2));
    REQUIRE_THAT(meta.bands.value(2).offset, WithinAbs(-1000.0, 0.1));
    REQUIRE_THAT(meta.bands.value(2).scale, WithinAbs(10000.0, 0.1));
    // Bands without an offset entry are not fabricated
    CHECK_FALSE(meta.bands.contains(1));
}

// ---------------------------------------------------------------------------
// #610: fail-closed sun elevation, BT NaN, Chavez DOS in reflectance space
// ---------------------------------------------------------------------------

TEST_CASE("TOA reflectance fails closed without SUN_ELEVATION (#610)", "[radcal][gdal]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("noSunEl.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("out.tif"));
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(sourcePath, 1, 1, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    GDALRasterBandH b1 = GDALGetRasterBand(srcDs, 1);
    std::vector<float> band = {10000.0f};
    REQUIRE(GDALRasterIO(b1, GF_Write, 0, 0, 1, 1, band.data(), 1, 1, GDT_Float32, 0, 0) == CE_None);
    GDALSetDescription(b1, "B4");
    GDALClose(srcDs);

    // SUN_ELEVATION intentionally absent.
    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("REFLECTANCE_MULT_BAND_4 = 0.00002"),
        QStringLiteral("REFLECTANCE_ADD_BAND_4 = 0.0"),
    });

    QString error;
    const bool ok = processFile(sourcePath, outputPath, mtlPath,
                                static_cast<int>(OutputUnit::ToaReflectance),
                                {}, &error);
    REQUIRE_FALSE(ok);
    REQUIRE(error.contains(QStringLiteral("SUN_ELEVATION")));
}

TEST_CASE("loadMetadata flags hasSunElevation (#610)", "[radcal][mtl]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString mtlPath = dir.filePath(QStringLiteral("LC08_MTL.txt"));

    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("SUN_ELEVATION = 42.0"),
        QStringLiteral("REFLECTANCE_MULT_BAND_4 = 0.00002"),
        QStringLiteral("REFLECTANCE_ADD_BAND_4 = 0.0"),
    });
    RadiometricCalibration::CalibrationMetadata meta;
    QString err;
    QMap<int, QString> bandNames;
    bandNames.insert(1, QStringLiteral("B4"));
    REQUIRE(RadiometricCalibration::loadMetadata(QString(), mtlPath, bandNames, &meta, &err));
    CHECK(meta.hasSunElevation);
    REQUIRE_THAT(meta.sunElevationDeg, WithinAbs(42.0, 1e-9));

    writeMtlFile(mtlPath, {
        QStringLiteral("SPACECRAFT_ID = \"LANDSAT_8\""),
        QStringLiteral("REFLECTANCE_MULT_BAND_4 = 0.00002"),
    });
    REQUIRE(RadiometricCalibration::loadMetadata(QString(), mtlPath, bandNames, &meta, &err));
    CHECK_FALSE(meta.hasSunElevation);
}

TEST_CASE("toBrightnessTemperature maps non-positive radiance to NaN, not 0 K (#610)", "[radcal]")
{
    // gain*bias chosen so the second DN maps to radiance <= 0.
    RadiometricCalibration::BandCoefficients c;
    c.radianceGain = 0.1;
    c.radianceBias = -10.0; // DN 50 -> L = -5 (invalid), DN 200 -> L = 10 (valid)
    c.k1 = 774.8853;
    c.k2 = 1321.0789;
    std::vector<float> dn = {200.0f, 50.0f};
    std::vector<float> out(2);
    REQUIRE(toBrightnessTemperature(dn.data(), out.data(), 2, c));
    REQUIRE(std::isfinite(out[0]));
    REQUIRE(std::isnan(out[1]));
}

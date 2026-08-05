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

TEST_CASE("toToaReflectance generic scale-offset formula", "[radcal]")
{
    // rho = (DN + offset) / scale
    std::vector<float> dn = {1000.0f, 5000.0f};
    std::vector<float> out(2);
    BandCoefficients c;
    c.scale = 10000.0;
    c.offset = 0.0;
    REQUIRE(toToaReflectance(dn.data(), out.data(), 2, c, SensorType::Generic, 90.0));
    REQUIRE_THAT(out[0], WithinAbs(0.1f, 0.001f));   // 1000/10000
    REQUIRE_THAT(out[1], WithinAbs(0.5f, 0.001f));   // 5000/10000
}

TEST_CASE("toToaReflectance generic rejects zero scale", "[radcal]")
{
    std::vector<float> dn = {100.0f};
    std::vector<float> out(1);
    BandCoefficients c;
    c.scale = 0.0;
    REQUIRE_FALSE(toToaReflectance(dn.data(), out.data(), 1, c, SensorType::Generic, 90.0));
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

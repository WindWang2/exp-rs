// test_cn_products.cpp — Chinese satellite product adapters (GF-1/2/6, ZY-3,
// HJ-1A/1B CCD): identity, CRESDA sidecar parsing, band-role tables, import
// operators and the headless GF-1 end-to-end chain (ADR 0146).
//
// All fixtures are synthetic (a few KB of XML + tiny GeoTIFFs); no real
// imagery is committed anywhere (tests synthesise at runtime).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include "geospatial/products/cn_product_metadata.h"
#include "geospatial/products/product_adapters.h"
#include "geospatial/products/product_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_gaofen_import_operator.h"
#include "operators/rs/rs_hj_import_operator.h"
#include "operators/rs/rs_zy3_import_operator.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#ifdef SICNU_HAS_OPENCV
#include "operators/rs/rs_supervised_classification_operator.h"
#endif

#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace sicnu::operators;
using namespace sicnu::geo;

namespace {

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_cn_products";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

void ensureGdalInit()
{
    GDALAllRegister();
    OGRRegisterAll();
}

/// Multi-band GeoTIFF at a GF-like UTM 49N grid.
void writeStackTiff(const QString &path, const std::vector<std::vector<float>> &bands,
                    double pixelSize = 8.0)
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);
    const int width = static_cast<int>(bands.front().size() == 0 ? 0 : std::sqrt(bands.front().size()));
    const int height = width;
    const std::array<double, 6> gt = {500000, pixelSize, 0, 4400000, 0, -pixelSize};
    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), width, height,
                                 static_cast<int>(bands.size()), GDT_Float32, nullptr);
    REQUIRE(ds != nullptr);
    GDALSetGeoTransform(ds, gt.data());
    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    OSRImportFromEPSG(srs, 32649);
    char *wkt = nullptr;
    OSRExportToWkt(srs, &wkt);
    GDALSetProjection(ds, wkt);
    CPLFree(wkt);
    OSRDestroySpatialReference(srs);
    for (int b = 0; b < static_cast<int>(bands.size()); ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
        REQUIRE(GDALRasterIO(band, GF_Write, 0, 0, width, height,
                             const_cast<float *>(bands[b].data()), width, height, GDT_Float32, 0, 0)
                == CE_None);
    }
    GDALClose(ds);
}

/// Minimal CRESDA L1A sidecar XML. `extraTags` are spliced before </MetaInfo>
/// so each test can declare (or deliberately omit) fields.
QString writeCresdaXml(const QString &path, const QString &productId,
                       const QString &satellite, const QString &sensor,
                       const QString &modeId, int bandCount,
                       const QStringList &extraTags = {},
                       double pixelSizeM = 8.0)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<MetaInfo>\n";
    out << "  <ProductID>" << productId << "</ProductID>\n";
    out << "  <SatelliteID>" << satellite << "</SatelliteID>\n";
    out << "  <SensorID>" << sensor << "</SensorID>\n";
    if (!modeId.isEmpty())
        out << "  <ModeID>" << modeId << "</ModeID>\n";
    out << "  <ReceiveDate>2024-04-15</ReceiveDate>\n";
    out << "  <ReceiveTime>11:23:45.123</ReceiveTime>\n";
    out << "  <StopDate>2024-04-15</StopDate>\n";
    out << "  <StopTime>11:24:02.800</StopTime>\n";
    out << "  <Width>16</Width>\n  <Height>16</Height>\n";
    out << "  <PixelSizeX>" << pixelSizeM << "</PixelSizeX>\n";
    out << "  <PixelSizeY>" << pixelSizeM << "</PixelSizeY>\n";
    for (int b = 1; b <= bandCount; ++b)
        out << "  <BandID>B" << b << "</BandID>\n";
    for (const QString &line : extraTags)
        out << "  " << line << "\n";
    out << "</MetaInfo>\n";
    file.close();
    return path;
}

/// GF-1 PMS product directory (multispectral + panchromatic pair). MS pixel
/// values: left half vegetation-like (high NIR, low RED), right half
/// water-like (low NIR, higher RED).
struct CnFixturePaths
{
    QString dir;
    QString msXml;
    QString msTiff;
    QString panXml;
    QString panTiff;
};

CnFixturePaths makeGaofenPmsDir(const QDir &root, const QStringList &msExtraTags = {},
                               const QString &productId =
                                 QStringLiteral("GF1_PMS1_E117.0_N40.0_20240415_L1A0001234567"))
{
    ensureApp();
    CnFixturePaths paths;
    paths.dir = root.filePath(QStringLiteral("GF1_PMS1_E117.0_N40.0_20240415_L1A0001234567-MSS1"));
    QDir().mkpath(paths.dir);

    std::vector<std::vector<float>> bands(4);
    for (int b = 0; b < 4; ++b)
        bands[b].resize(16 * 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool vegetation = x < 8;
            bands[0][y * 16 + x] = vegetation ? 400.f : 300.f; // B1 blue
            bands[1][y * 16 + x] = vegetation ? 500.f : 250.f; // B2 green
            bands[2][y * 16 + x] = vegetation ? 350.f : 600.f; // B3 red
            bands[3][y * 16 + x] = vegetation ? 2400.f : 150.f; // B4 nir
        }
    }
    paths.msTiff = paths.dir + QStringLiteral("/GF1_PMS1_E117.0_N40.0_20240415_L1A0001234567-MSS1.tiff");
    writeStackTiff(paths.msTiff, bands);

    paths.msXml = writeCresdaXml(paths.dir +
                                   QStringLiteral("/GF1_PMS1_E117.0_N40.0_20240415_L1A0001234567-MSS1.xml"),
                                 productId, QStringLiteral("GF1"), QStringLiteral("PMS1"),
                                 QStringLiteral("MSS"), 4, msExtraTags);

    // Panchromatic companion (same grid, one band).
    std::vector<std::vector<float>> pan(1, std::vector<float>(16 * 16, 900.f));
    paths.panTiff = paths.dir + QStringLiteral("/GF1_PMS1_E117.0_N40.0_20240415_L1A0001234567-PAN1.tiff");
    writeStackTiff(paths.panTiff, pan, 2.0);
    paths.panXml = writeCresdaXml(paths.dir +
                                    QStringLiteral("/GF1_PMS1_E117.0_N40.0_20240415_L1A0001234567-PAN1.xml"),
                                  productId + QStringLiteral("-PAN1"), QStringLiteral("GF1"),
                                  QStringLiteral("PMS1"), QStringLiteral("PAN"), 1);
    return paths;
}

const sicnu::geo::BandCalibration *findCalibration(const ProductMetadata &metadata,
                                                   const std::string &band)
{
    for (const sicnu::geo::BandCalibration &entry : metadata.bandCalibration) {
        if (entry.band == band)
            return &entry;
    }
    return nullptr;
}

} // namespace

// NOTE: first loader test — it must run before any family file is cached.
TEST_CASE("cn_products: Band-role table loader fails closed and diagnoses", "[cn][roles]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // The env override must WIN (not fall through to the in-tree tables), so
    // create the directory it points at but leave it empty: the loader then
    // reports the missing table file instead of guessing.
    QDir().mkpath(tmp.path() + QStringLiteral("/products/band_roles"));
    qputenv("SICNU_DATA_DIR", tmp.path().toUtf8());
    REQUIRE(cnBandRoleTableDir().find("band_roles") != std::string::npos);
    REQUIRE(cnBandRoleTableDir().find("band_roles/gaofen.json") == std::string::npos);
    try {
        cnBandRoleTable("gf1_pms");
        FAIL("expected GeoError for missing table");
    } catch (const GeoError &) {
        // diagnosable refusal, not a silent unknown
    }
    qunsetenv("SICNU_DATA_DIR");
}

TEST_CASE("cn_products: Unsupported Chinese satellite families are refused diagnosably", "[cn][identity]")
{
    ensureApp();

    auto checkRefused = [](const char *path) {
        const CnProductIdentity identity = cnIdentifyProduct(path);
        INFO("path: " << path);
        REQUIRE(identity.recognized);
        REQUIRE_FALSE(identity.supported);
        REQUIRE(!identity.reason.empty());

        REQUIRE(detectProductKind(path) == ProductKind::Unknown);
        bool threw = false;
        try {
            (void)readProductMetadataAuto(path);
        } catch (const GeoError &error) {
            threw = true;
            REQUIRE(error.code() == sicnu::geo::ErrorCode::UnsupportedProduct);
            REQUIRE(std::string(error.what()).find_first_not_of(" \n") != std::string::npos);
        }
        REQUIRE(threw);
    };

    checkRefused("/data/GF3_KS_E117.0_N40.0_20240415_L1A_HH_HV.tiff");
    checkRefused("/data/GF4_PMS_E117.0_N40.0_20240415_L1A.tiff");
    checkRefused("/data/GF5_AHSI_E117.0_N40.0_20240415_L1A.tiff");
    checkRefused("/data/GF7_FWD_E117.0_N40.0_20240415_L1A.tiff");
    checkRefused("/data/ZY1_02C_E117.0_N40.0_20240415.tiff");
    checkRefused("/data/HJ2A-HSI-1-450-20240415-L1A-12345-1.tiff");
    checkRefused("/data/HJ1A-IRS-1-450-20240415-L1A-12345-1.tiff");
    checkRefused("/data/CBERS4_MUX_20240415.tiff");

    // Unrecognized names are NOT claimed as CN at all.
    const CnProductIdentity plain = cnIdentifyProduct("/data/some_random.tif");
    REQUIRE_FALSE(plain.recognized);
    REQUIRE_FALSE(plain.supported);
}

TEST_CASE("cn_products: GF-1 PMS detection, metadata and band roles", "[cn][gaofen]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const CnFixturePaths fixture = makeGaofenPmsDir(
        QDir(tmp.path()),
        {"<CloudPercent>12</CloudPercent>",
         "<OrbitID>28861</OrbitID>",
         "<SunPosGeodetic><Azimuth>157.80</Azimuth><Elevation>62.90</Elevation></SunPosGeodetic>"});

    REQUIRE(detectProductKind(fixture.msXml.toStdString()) == ProductKind::GaofenProduct);
    REQUIRE(detectProductKind(fixture.msTiff.toStdString()) == ProductKind::GaofenProduct);
    REQUIRE(detectProductKind(fixture.dir.toStdString()) == ProductKind::GaofenProduct);

    // Registry claims the product (never the GenericRaster fallback).
    ProductAdapter *adapter = ProductAdapterRegistry::instance().adapterFor(fixture.dir.toStdString());
    REQUIRE(adapter != nullptr);
    REQUIRE(adapter->id() == "gaofen_product");
    const ProductAssets assets = adapter->enumerate(fixture.dir.toStdString());
    REQUIRE(assets.kind == ProductKind::GaofenProduct);
    REQUIRE(assets.completeness == ProductCompleteness::Complete);
    REQUIRE(assets.adapterId == "gaofen_product");
    REQUIRE(assets.productId == "GF1_PMS1_E117.0_N40.0_20240415_L1A0001234567");

    // Declared metadata, verbatim.
    const ProductMetadata &metadata = assets.metadata;
    REQUIRE(metadata.platform == "GF1");
    REQUIRE(metadata.sensor == "PMS1");
    REQUIRE(metadata.processingLevel == "L1A");
    REQUIRE(metadata.acquisitionTime == "2024-04-15T11:23:45.123");
    REQUIRE(metadata.hasCloudCover);
    REQUIRE(metadata.cloudCover == Catch::Approx(12.0));
    REQUIRE(metadata.hasResolution);
    REQUIRE(metadata.resolutionMeters == Catch::Approx(8.0));
    REQUIRE(metadata.radiometricState == "digital_number");
    REQUIRE(metadata.modality == "optical");
    REQUIRE(metadata.orbitId == "28861");
    REQUIRE(metadata.hasSunElevation);
    REQUIRE(metadata.sunElevationDeg == Catch::Approx(62.90));
    REQUIRE(metadata.hasSunAzimuth);
    REQUIRE(metadata.sunAzimuthDeg == Catch::Approx(157.80));
    REQUIRE(metadata.declaredBandIds == std::vector<std::string>({"B1", "B2", "B3", "B4"}));

    // Band roles through the data-driven table.
    REQUIRE(assets.assets.size() == 5); // 4 bands + sidecar metadata asset
    const char *expectedRoles[] = {"blue", "green", "red", "nir"};
    const double expectedWavelengths[] = {470.0, 560.0, 650.0, 825.0};
    for (int i = 0; i < 4; ++i) {
        const ProductAsset &asset = assets.assets[i];
        INFO("band " << asset.nativeBandName);
        REQUIRE(asset.role == "measurement");
        REQUIRE(asset.nativeBandName == std::string("B") + std::to_string(i + 1));
        REQUIRE(asset.bandRole == expectedRoles[i]);
        REQUIRE(asset.hasWavelength);
        REQUIRE(asset.wavelengthNm == Catch::Approx(expectedWavelengths[i]));
    }

    // PMS directory: the multispectral sidecar is preferred (teaching default).
    REQUIRE(assets.metadata.sensorMode == "PMS1");
}

TEST_CASE("cn_products: GF-1 PMS panchromatic product maps to the panchromatic role", "[cn][gaofen]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const CnFixturePaths fixture = makeGaofenPmsDir(QDir(tmp.path()));

    const ProductAssets assets =
      ProductAdapterRegistry::instance().describe(fixture.panXml.toStdString());
    REQUIRE(assets.kind == ProductKind::GaofenProduct);
    REQUIRE(assets.completeness == ProductCompleteness::Complete);
    REQUIRE(assets.metadata.sensorMode == "PMS1");
    REQUIRE(assets.notes["sensor_key"].asString() == "gf1_pms_pan");
    REQUIRE(assets.assets.size() == 2); // 1 band + sidecar
    REQUIRE(assets.assets[0].bandRole == "panchromatic");
    REQUIRE(assets.assets[0].wavelengthNm == Catch::Approx(675.0));
}

TEST_CASE("cn_products: GF-2 PMS and GF-6 WFV layouts", "[cn][gaofen]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    SECTION("GF-2 PMS multispectral") {
        const QString dir = tmp.path() + QStringLiteral("/GF2_PMS2_E102.0_N36.0_20240301_L1A0009876543-MSS1");
        QDir().mkpath(dir);
        writeStackTiff(dir + QStringLiteral("/GF2_PMS2_E102.0_N36.0_20240301_L1A0009876543-MSS1.tiff"),
                       std::vector<std::vector<float>>(4, std::vector<float>(256, 100.f)));
        writeCresdaXml(dir + QStringLiteral("/GF2_PMS2_E102.0_N36.0_20240301_L1A0009876543-MSS1.xml"),
                       "GF2_PMS2_E102.0_N36.0_20240301_L1A0009876543", "GF2", "PMS2", "MSS", 4);
        const ProductMetadata metadata = readProductMetadataAuto(dir.toStdString());
        REQUIRE(metadata.platform == "GF2");
        REQUIRE(metadata.sensor == "PMS2");
        const CnBandRoleTable table = cnBandRoleTable("gf2_pms");
        REQUIRE(table.bands.size() == 4);
        REQUIRE(table.bands[1].role == "green");
        REQUIRE(table.bands[1].wavelengthNm == Catch::Approx(555.0));
    }

    SECTION("GF-6 WFV 8 bands; violet/yellow are explicitly unknown") {
        const QString dir = tmp.path() + QStringLiteral("/GF6_WFV6_E102.0_N36.0_20240301_L1A0009876543");
        QDir().mkpath(dir);
        writeStackTiff(dir + QStringLiteral("/GF6_WFV6_E102.0_N36.0_20240301_L1A0009876543.tiff"),
                       std::vector<std::vector<float>>(8, std::vector<float>(256, 100.f)), 16.0);
        writeCresdaXml(dir + QStringLiteral("/GF6_WFV6_E102.0_N36.0_20240301_L1A0009876543.xml"),
                       "GF6_WFV6_E102.0_N36.0_20240301_L1A0009876543", "GF6", "WFV6", "WFV", 8,
                       {}, 16.0);

        REQUIRE(detectProductKind(dir.toStdString()) == ProductKind::GaofenProduct);
        const ProductAssets assets =
          ProductAdapterRegistry::instance().describe(dir.toStdString());
        REQUIRE(assets.adapterId == "gaofen_product");
        REQUIRE(assets.completeness == ProductCompleteness::Complete);
        REQUIRE(assets.metadata.declaredBandIds.size() == 8);

        const CnBandRoleTable table = cnBandRoleTable("gf6_wfv");
        REQUIRE(table.bands.size() == 8);
        REQUIRE(table.bands[0].role == "blue");
        REQUIRE(table.bands[3].role == "nir");
        REQUIRE(table.bands[4].role == "red_edge");
        REQUIRE(table.bands[5].role == "red_edge");
        REQUIRE(table.bands[6].role == "unknown");
        REQUIRE(!table.bands[6].roleReason.empty());
        REQUIRE(table.bands[7].role == "unknown");
        REQUIRE(!table.bands[7].roleReason.empty());

        // The refusal reason surfaces on the enumerated assets too.
        std::vector<std::string> noteKeys = assets.notes.getMemberNames();
        bool sawUnknownNote = false;
        for ( const std::string &key : noteKeys )
        {
            if ( key.rfind( "role_B7", 0 ) == 0 || key.rfind( "role_B8", 0 ) == 0 )
                sawUnknownNote = true;
        }
        REQUIRE( sawUnknownNote );
    }
}

TEST_CASE("cn_products: ZY-3 NAD multispectral and panchromatic", "[cn][zy3]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    SECTION("NAD multispectral resolves zy3_nad_ms") {
        const QString dir = tmp.path() + QStringLiteral("/ZY3_NAD_E117.0_N40.0_20240301_L1A0005551234-MSS1");
        QDir().mkpath(dir);
        writeStackTiff(dir + QStringLiteral("/ZY3_NAD_E117.0_N40.0_20240301_L1A0005551234-MSS1.tiff"),
                       std::vector<std::vector<float>>(4, std::vector<float>(256, 90.f)), 5.8);
        writeCresdaXml(dir + QStringLiteral("/ZY3_NAD_E117.0_N40.0_20240301_L1A0005551234-MSS1.xml"),
                       "ZY3_NAD_E117.0_N40.0_20240301_L1A0005551234", "ZY3", "NAD", "MSS", 4,
                       {}, 5.8);
        const ProductAssets assets =
          ProductAdapterRegistry::instance().describe(dir.toStdString());
        REQUIRE(assets.kind == ProductKind::Zy3Product);
        REQUIRE(assets.adapterId == "zy3_product");
        REQUIRE(assets.completeness == ProductCompleteness::Complete);
        REQUIRE(assets.metadata.platform == "ZY3");
        REQUIRE(assets.notes["sensor_key"].asString() == "zy3_nad_ms");
        REQUIRE(assets.assets.size() == 5);
        const char *expectedRoles[] = {"blue", "green", "red", "nir"};
        for (int i = 0; i < 4; ++i)
            REQUIRE(assets.assets[i].bandRole == expectedRoles[i]);
    }

    SECTION("TLC panchromatic resolves zy3_pan") {
        const QString dir = tmp.path() + QStringLiteral("/ZY3_TLC_E117.0_N40.0_20240301_L1A0005551234-TLC1");
        QDir().mkpath(dir);
        writeStackTiff(dir + QStringLiteral("/ZY3_TLC_E117.0_N40.0_20240301_L1A0005551234-TLC1.tiff"),
                       std::vector<std::vector<float>>(1, std::vector<float>(256, 200.f)), 2.1);
        writeCresdaXml(dir + QStringLiteral("/ZY3_TLC_E117.0_N40.0_20240301_L1A0005551234-TLC1.xml"),
                       "ZY3_TLC_E117.0_N40.0_20240301_L1A0005551234", "ZY3", "TLC", "PAN", 1,
                       {}, 2.1);
        const ProductAssets assets =
          ProductAdapterRegistry::instance().describe(dir.toStdString());
        REQUIRE(assets.kind == ProductKind::Zy3Product);
        REQUIRE(assets.notes["sensor_key"].asString() == "zy3_pan");
        REQUIRE(assets.assets.size() == 2);
        REQUIRE(assets.assets[0].bandRole == "panchromatic");
        REQUIRE(assets.assets[0].wavelengthNm == Catch::Approx(550.0));
    }
}

TEST_CASE("cn_products: HJ-1A/1B CCD detection and metadata", "[cn][hj]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/HJ1B-CCD2-F1-450-20240401-L1A-1234567890-1");
    QDir().mkpath(dir);
    writeStackTiff(dir + QStringLiteral("/HJ1B-CCD2-F1-450-20240401-L1A-1234567890-1.tiff"),
                   std::vector<std::vector<float>>(4, std::vector<float>(256, 80.f)), 30.0);
    writeCresdaXml(dir + QStringLiteral("/HJ1B-CCD2-F1-450-20240401-L1A-1234567890-1.xml"),
                   "HJ1B-CCD2-F1-450-20240401-L1A-1234567890-1", "HJ1B", "CCD2", "CCD", 4,
                   {}, 30.0);

    REQUIRE(detectProductKind(dir.toStdString()) == ProductKind::HjCcdProduct);
    const ProductAssets assets = ProductAdapterRegistry::instance().describe(dir.toStdString());
    REQUIRE(assets.kind == ProductKind::HjCcdProduct);
    REQUIRE(assets.adapterId == "hj_ccd_product");
    REQUIRE(assets.completeness == ProductCompleteness::Complete);
    REQUIRE(assets.metadata.platform == "HJ1B");
    REQUIRE(assets.metadata.sensor == "CCD2");
    REQUIRE(assets.metadata.hasResolution);
    REQUIRE(assets.metadata.resolutionMeters == Catch::Approx(30.0));
    const char *expectedRoles[] = {"blue", "green", "red", "nir"};
    for (int i = 0; i < 4; ++i)
        REQUIRE(assets.assets[i].bandRole == expectedRoles[i]);
}

TEST_CASE("cn_products: Absent sidecar fields stay explicitly absent", "[cn][absence]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const CnFixturePaths fixture = makeGaofenPmsDir(QDir(tmp.path())); // no cloud/sun/orbit tags

    const ProductMetadata metadata = readProductMetadataAuto(fixture.msXml.toStdString());
    REQUIRE_FALSE(metadata.hasCloudCover);
    REQUIRE_FALSE(metadata.hasSunElevation);
    REQUIRE_FALSE(metadata.hasSunAzimuth);
    REQUIRE(metadata.orbitId.empty());
    REQUIRE(metadata.bandCalibration.empty());

    const Json::Value json = metadata.toJson();
    REQUIRE(json["has_sun_elevation"].asBool() == false);
    REQUIRE(!json.isMember("sun_elevation_deg"));
    REQUIRE(!json.isMember("band_calibration"));
    REQUIRE(!json.isMember("orbit_id"));
    REQUIRE(!json.isMember("cloud_cover"));
}

TEST_CASE("cn_products: Declared calibration is carried verbatim", "[cn][calibration]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    SECTION("flat GainVal/OffsetVal lists") {
        const CnFixturePaths fixture = makeGaofenPmsDir(
            QDir(tmp.path()),
            {"<GainVal>0.5,0.6,0.7,0.8</GainVal>", "<OffsetVal>-1.0,-2.0,-3.0,-4.0</OffsetVal>"});
        const ProductMetadata metadata = readProductMetadataAuto(fixture.msXml.toStdString());
        REQUIRE(metadata.bandCalibration.size() == 4);
        const sicnu::geo::BandCalibration *b1 = findCalibration(metadata, "B1");
        REQUIRE(b1 != nullptr);
        REQUIRE(b1->hasGain);
        REQUIRE(b1->gain == Catch::Approx(0.5));
        REQUIRE(b1->hasBias);
        REQUIRE(b1->bias == Catch::Approx(-1.0));
        const sicnu::geo::BandCalibration *b4 = findCalibration(metadata, "B4");
        REQUIRE(b4 != nullptr);
        REQUIRE(b4->gain == Catch::Approx(0.8));
        REQUIRE(b4->bias == Catch::Approx(-4.0));
    }

    SECTION("per-band BandCalibration elements") {
        const CnFixturePaths fixture = makeGaofenPmsDir(
            QDir(tmp.path()),
            {"<BandCalibration><BandID>B1</BandID><Gain>0.55</Gain></BandCalibration>",
             "<BandCalibration><BandID>B2</BandID><Gain>0.66</Gain></BandCalibration>",
             "<BandCalibration><BandID>B3</BandID><Gain>0.77</Gain></BandCalibration>",
             "<BandCalibration><BandID>B4</BandID><Gain>0.88</Gain><Offset>-2.0</Offset></BandCalibration>"});
        const ProductMetadata metadata = readProductMetadataAuto(fixture.msXml.toStdString());
        REQUIRE(metadata.bandCalibration.size() == 4);
        const sicnu::geo::BandCalibration *b1 = findCalibration(metadata, "B1");
        REQUIRE(b1 != nullptr);
        REQUIRE(b1->gain == Catch::Approx(0.55));
        REQUIRE_FALSE(b1->hasBias); // gain-only band: bias stays absent
        const sicnu::geo::BandCalibration *b4 = findCalibration(metadata, "B4");
        REQUIRE(b4 != nullptr);
        REQUIRE(b4->gain == Catch::Approx(0.88));
        REQUIRE(b4->bias == Catch::Approx(-2.0));
    }
}

TEST_CASE("cn_products: rs:gaofen_import stacks roles, sun geometry and calibration", "[cn][operators][gaofen]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const CnFixturePaths fixture = makeGaofenPmsDir(
        QDir(tmp.path()),
        {"<CloudPercent>5</CloudPercent>", "<OrbitID>28861</OrbitID>",
         "<SunPosGeodetic><Azimuth>157.80</Azimuth><Elevation>62.90</Elevation></SunPosGeodetic>",
         "<GainVal>0.5,0.6,0.7,0.8</GainVal>", "<OffsetVal>-1.0,-2.0,-3.0,-4.0</OffsetVal>"});

    auto op = RSOperatorRegistry::instance().create("rs:gaofen_import");
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;
    const QString output = tmp.path() + QStringLiteral("/gf1_stack.tif");
    Json::Value params(Json::objectValue);
    params["input"] = fixture.dir.toStdString();
    params["output"] = output.toStdString();
    Json::Value result;
    REQUIRE_NOTHROW(result = op->run(params, ctx));

    REQUIRE(result["output"].asString() == output.toStdString());
    REQUIRE(result["productKind"].asString() == "gaofen_product");
    REQUIRE(result["satellite"].asString() == "GF1");
    REQUIRE(result["bandCount"].asInt() == 4);
    REQUIRE(result["radiometricState"].asString() == "digital_number");
    REQUIRE(result["declared"]["sunElevationDeg"].asBool() == true);
    REQUIRE(result["declared"]["calibration"].asBool() == true);
    REQUIRE(result["missingDeclaredFields"].size() == 0);

    // Stacked output metadata.
    GDALDatasetH ds = GDALOpen(output.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);
    REQUIRE(GDALGetMetadataItem(ds, "SICNU_PRODUCT_TYPE", nullptr) != nullptr);
    REQUIRE(std::string(GDALGetMetadataItem(ds, "SICNU_PRODUCT_TYPE", nullptr)) == "gaofen_product");
    REQUIRE(std::string(GDALGetMetadataItem(ds, "SICNU_PRODUCT_FAMILY", nullptr)) == "cn");
    REQUIRE(std::string(GDALGetMetadataItem(ds, "SICNU_RADIOMETRIC_STATE", nullptr)) == "digital_number");
    REQUIRE(std::string(GDALGetMetadataItem(ds, "SICNU_SUN_ELEVATION_DEG", nullptr)) == "62.9000");
    REQUIRE(std::string(GDALGetMetadataItem(ds, "SICNU_SPACECRAFT", nullptr)) == "GF1");
    REQUIRE(std::string(GDALGetMetadataItem(ds, "SICNU_ORBIT_ID", nullptr)) == "28861");

    const char *expectedRoles[] = {"blue", "green", "red", "nir"};
    const char *expectedWavelengths[] = {"470", "560", "650", "825"};
    const char *expectedGains[] = {"0.5", "0.6", "0.7", "0.8"};
    for (int b = 1; b <= 4; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b);
        INFO("stacked band " << b);
        REQUIRE(std::string(GDALGetMetadataItem(band, "SICNU_BAND_ROLE", nullptr))
                == expectedRoles[b - 1]);
        REQUIRE(std::string(GDALGetMetadataItem(band, "WAVELENGTH", nullptr))
                == expectedWavelengths[b - 1]);
        REQUIRE(std::string(GDALGetMetadataItem(band, "WAVELENGTH_UNITS", nullptr)) == "nm");
        REQUIRE(std::string(GDALGetMetadataItem(band, ("SICNU_CALIB_GAIN_B" + std::to_string(b)).c_str(),
                                             nullptr))
                == expectedGains[b - 1]);
    }
    GDALClose(ds);
}

TEST_CASE("cn_products: rs:gaofen_import with per-band BandCalibration sidecar keeps inventory clean",
          "[cn][operators][gaofen][calibration]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // Per-band calibration shape: <BandCalibration><BandID>B1</BandID><Gain>…
    // The nested BandID elements must NOT pollute the declared inventory
    // (regression: flat "bandid" watch matched at any depth and duplicated
    // the inventory, breaking the stack at sourceBand 5).
    const CnFixturePaths fixture = makeGaofenPmsDir(
        QDir(tmp.path()),
        {"<BandCalibration><BandID>B1</BandID><Gain>0.55</Gain></BandCalibration>",
         "<BandCalibration><BandID>B2</BandID><Gain>0.66</Gain></BandCalibration>",
         "<BandCalibration><BandID>B3</BandID><Gain>0.77</Gain></BandCalibration>",
         "<BandCalibration><BandID>B4</BandID><Gain>0.88</Gain><Offset>-2.0</Offset></BandCalibration>"});

    auto op = RSOperatorRegistry::instance().create("rs:gaofen_import");
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;
    const QString output = tmp.path() + QStringLiteral("/gf1_calib.tif");
    Json::Value params(Json::objectValue);
    params["input"] = fixture.dir.toStdString();
    params["output"] = output.toStdString();
    REQUIRE_NOTHROW((void)op->run(params, ctx));

    GDALDatasetH ds = GDALOpen(output.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);
    REQUIRE(GDALGetRasterCount(ds) == 4); // inventory not duplicated
    REQUIRE(std::string(GDALGetMetadataItem(GDALGetRasterBand(ds, 1), "SICNU_CALIB_GAIN_B1", nullptr))
            == "0.55");
    REQUIRE(std::string(GDALGetMetadataItem(GDALGetRasterBand(ds, 4), "SICNU_CALIB_GAIN_B4", nullptr))
            == "0.88");
    REQUIRE(std::string(GDALGetMetadataItem(GDALGetRasterBand(ds, 4), "SICNU_CALIB_BIAS_B4", nullptr))
            == "-2");
    // Reordered/subset request: coefficients follow the STACKED band order.
    GDALClose(ds);
    const QString output2 = tmp.path() + QStringLiteral("/gf1_calib_reordered.tif");
    Json::Value params2(Json::objectValue);
    params2["input"] = fixture.dir.toStdString();
    params2["output"] = output2.toStdString();
    Json::Value bands(Json::arrayValue);
    bands.append("B2");
    bands.append("B1");
    params2["bands"] = bands;
    REQUIRE_NOTHROW((void)op->run(params2, ctx));
    GDALDatasetH ds2 = GDALOpen(output2.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds2 != nullptr);
    REQUIRE(GDALGetRasterCount(ds2) == 2);
    // Output band 1 holds B2 pixels -> B2's gain must be on band 1.
    REQUIRE(std::string(GDALGetMetadataItem(GDALGetRasterBand(ds2, 1), "SICNU_CALIB_GAIN_B2", nullptr))
            == "0.66");
    REQUIRE(std::string(GDALGetMetadataItem(GDALGetRasterBand(ds2, 2), "SICNU_CALIB_GAIN_B1", nullptr))
            == "0.55");
    GDALClose(ds2);
}

TEST_CASE("cn_products: rs:gaofen_import fails closed on unknown bands", "[cn][operators][gaofen]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const CnFixturePaths fixture = makeGaofenPmsDir(QDir(tmp.path()));

    auto op = RSOperatorRegistry::instance().create("rs:gaofen_import");
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = fixture.dir.toStdString();
    params["output"] = (tmp.path() + QStringLiteral("/out.tif")).toStdString();
    Json::Value bands(Json::arrayValue);
    bands.append("B9");
    params["bands"] = bands;
    try {
        (void)op->run(params, ctx);
        FAIL("expected RSOperatorError");
    } catch (const RSOperatorError &error) {
        REQUIRE(error.code() == sicnu::operators::ErrorCode::InvalidInputData);
        REQUIRE(std::string(error.what()).find("B9") != std::string::npos);
    }
}

TEST_CASE("cn_products: rs:zy3_import and rs:hj_import stack role-tagged outputs", "[cn][operators][zy3][hj]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    SECTION("ZY-3 NAD multispectral") {
        const QString dir = tmp.path() + QStringLiteral("/ZY3_NAD_E117.0_N40.0_20240301_L1A0005551234-MSS1");
        QDir().mkpath(dir);
        writeStackTiff(dir + QStringLiteral("/ZY3_NAD_E117.0_N40.0_20240301_L1A0005551234-MSS1.tiff"),
                       std::vector<std::vector<float>>(4, std::vector<float>(256, 90.f)), 5.8);
        writeCresdaXml(dir + QStringLiteral("/ZY3_NAD_E117.0_N40.0_20240301_L1A0005551234-MSS1.xml"),
                       "ZY3_NAD_E117.0_N40.0_20240301_L1A0005551234", "ZY3", "NAD", "MSS", 4,
                       {}, 5.8);

        auto op = RSOperatorRegistry::instance().create("rs:zy3_import");
        REQUIRE(op != nullptr);
        RSOperatorContext ctx;
        Json::Value params(Json::objectValue);
        params["input"] = dir.toStdString();
        params["output"] = (tmp.path() + QStringLiteral("/zy3_stack.tif")).toStdString();
        Json::Value result;
        REQUIRE_NOTHROW(result = op->run(params, ctx));
        REQUIRE(result["productKind"].asString() == "zy3_product");
        REQUIRE(result["bandCount"].asInt() == 4);
        REQUIRE(result["bandRoles"][0].asString() == "blue");
        REQUIRE(result["bandRoles"][3].asString() == "nir");
        REQUIRE(QFile::exists(params["output"].asCString()));
    }

    SECTION("HJ-1A CCD") {
        const QString dir = tmp.path() + QStringLiteral("/HJ1A-CCD1-F1-450-20240401-L1A-1234567890-1");
        QDir().mkpath(dir);
        writeStackTiff(dir + QStringLiteral("/HJ1A-CCD1-F1-450-20240401-L1A-1234567890-1.tiff"),
                       std::vector<std::vector<float>>(4, std::vector<float>(256, 80.f)), 30.0);
        writeCresdaXml(dir + QStringLiteral("/HJ1A-CCD1-F1-450-20240401-L1A-1234567890-1.xml"),
                       "HJ1A-CCD1-F1-450-20240401-L1A-1234567890-1", "HJ1A", "CCD1", "CCD", 4);

        auto op = RSOperatorRegistry::instance().create("rs:hj_import");
        REQUIRE(op != nullptr);
        RSOperatorContext ctx;
        Json::Value params(Json::objectValue);
        params["input"] = dir.toStdString();
        params["output"] = (tmp.path() + QStringLiteral("/hj_stack.tif")).toStdString();
        Json::Value result;
        REQUIRE_NOTHROW(result = op->run(params, ctx));
        REQUIRE(result["productKind"].asString() == "hj_ccd_product");
        REQUIRE(result["bandCount"].asInt() == 4);
        REQUIRE(QFile::exists(params["output"].asCString()));

        GDALDatasetH ds = GDALOpen(params["output"].asCString(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        REQUIRE(std::string(GDALGetMetadataItem(ds, "SICNU_PRODUCT_TYPE", nullptr))
                == "hj_ccd_product");
        GDALClose(ds);
    }
}

TEST_CASE("cn_products: Current-generation ProductMetaData sidecar (Bands/ImageGSD/SolarZenith/CenterTime)",
          "[cn][gaofen][gencurrent]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // Real 2020+ CRESDA distribution shape (root <ProductMetaData>): no
    // BandID/PixelSizeX/SunPosGeodetic — instead Bands/ImageGSD/SolarZenith/
    // CenterTime. CenterTime is the imaging time; receive time (Beijing) is
    // deliberately declared too and must NOT win.
    const QString dir = tmp.path() + QStringLiteral("/GF1_WFV3_E113.0_N34.0_20220624_L1A0006547030");
    QDir().mkpath(dir);
    writeStackTiff(dir + QStringLiteral("/GF1_WFV3_E113.0_N34.0_20220624_L1A0006547030.tiff"),
                   std::vector<std::vector<float>>(4, std::vector<float>(256, 100.f)), 16.0);
    QFile file(dir + QStringLiteral("/GF1_WFV3_E113.0_N34.0_20220624_L1A0006547030.xml"));
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"(<?xml version="1.0" encoding="UTF-8"?>
<ProductMetaData>
  <ProductID>32869</ProductID>
  <SatelliteID>GF1</SatelliteID>
  <SensorID>WFV3</SensorID>
  <ProductLevel>L1A</ProductLevel>
  <Bands>1,2,3,4</Bands>
  <ImageGSD>16</ImageGSD>
  <WidthInPixels>16</WidthInPixels>
  <HeightInPixels>16</HeightInPixels>
  <CenterTime>2022-06-24 13:39:09.0</CenterTime>
  <StartTime>2022-06-24 13:38:51.0</StartTime>
  <EndTime>2022-06-24 13:39:27.0</EndTime>
  <ReceiveTime>2022-06-24 05:39:09.0</ReceiveTime>
  <SolarAzimuth>106.266</SolarAzimuth>
  <SolarZenith>25.1707</SolarZenith>
</ProductMetaData>
)");
    file.close();

    REQUIRE(detectProductKind(dir.toStdString()) == ProductKind::GaofenProduct);
    const ProductMetadata metadata = readProductMetadataAuto(dir.toStdString());
    REQUIRE(metadata.platform == "GF1");
    REQUIRE(metadata.sensor == "WFV3");
    REQUIRE(metadata.processingLevel == "L1A");
    REQUIRE(metadata.radiometricState == "digital_number");
    REQUIRE(metadata.hasResolution);
    REQUIRE(metadata.resolutionMeters == Catch::Approx(16.0));
    // Imaging time (CenterTime), normalized to ISO-8601 — not the 8h-off
    // Beijing receive time.
    REQUIRE(metadata.acquisitionTime == "2022-06-24T13:39:09.0");
    REQUIRE(metadata.declaredBandIds
            == std::vector<std::string>({"B1", "B2", "B3", "B4"}));
    // Zenith → elevation derived and the derivation reported.
    REQUIRE(metadata.hasSunElevation);
    REQUIRE(metadata.sunElevationDeg == Catch::Approx(90.0 - 25.1707).margin(1e-6));
    REQUIRE(metadata.hasSunAzimuth);
    REQUIRE(metadata.sunAzimuthDeg == Catch::Approx(106.266));

    // Import: band order is declared (Bands list), result says so.
    auto op = RSOperatorRegistry::instance().create("rs:gaofen_import");
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;
    const QString output = tmp.path() + QStringLiteral("/gf1_wfv_stack.tif");
    Json::Value params(Json::objectValue);
    params["input"] = dir.toStdString();
    params["output"] = output.toStdString();
    REQUIRE_NOTHROW((void)op->run(params, ctx));
    GDALDatasetH ds = GDALOpen(output.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);
    REQUIRE(GDALGetRasterCount(ds) == 4);
    REQUIRE(std::string(GDALGetMetadataItem(GDALGetRasterBand(ds, 4), "SICNU_BAND_ROLE", nullptr))
            == "nir");
    GDALClose(ds);
}

TEST_CASE("cn_products: GF-1 headless end-to-end: import → NDVI → supervised classification",
          "[cn][e2e][gaofen]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const CnFixturePaths fixture = makeGaofenPmsDir(
        QDir(tmp.path()),
        {"<CloudPercent>5</CloudPercent>",
         "<SunPosGeodetic><Azimuth>157.80</Azimuth><Elevation>62.90</Elevation></SunPosGeodetic>"});

    // 1. Import (analysis-ready stacking with roles).
    auto importOp = RSOperatorRegistry::instance().create("rs:gaofen_import");
    REQUIRE(importOp != nullptr);
    RSOperatorContext ctx;
    const QString stacked = tmp.path() + QStringLiteral("/gf1_analysis_ready.tif");
    Json::Value importParams(Json::objectValue);
    importParams["input"] = fixture.dir.toStdString();
    importParams["output"] = stacked.toStdString();
    REQUIRE_NOTHROW((void)importOp->run(importParams, ctx));
    REQUIRE(QFile::exists(stacked));

    // 2. NDVI from band roles (no positional band parameters).
    auto ndviOp = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(ndviOp != nullptr);
    const QString ndvi = tmp.path() + QStringLiteral("/gf1_ndvi.tif");
    Json::Value ndviParams(Json::objectValue);
    ndviParams["input"] = stacked.toStdString();
    ndviParams["output"] = ndvi.toStdString();
    ndviParams["index"] = "NDVI";
    REQUIRE_NOTHROW((void)ndviOp->run(ndviParams, ctx));
    REQUIRE(QFile::exists(ndvi));

    // Vegetation side strongly positive, water side negative.
    {
        GdalDatasetWrapper ndviDs;
        REQUIRE(ndviDs.open(ndvi));
        std::vector<float> pixels(16 * 16);
        REQUIRE(ndviDs.readBandData(1, pixels.data(), 16, 16));
        double vegetationSum = 0.0;
        double waterSum = 0.0;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                if (x < 8)
                    vegetationSum += pixels[y * 16 + x];
                else
                    waterSum += pixels[y * 16 + x];
            }
        }
        REQUIRE(vegetationSum / 128.0 > 0.5);
        REQUIRE(waterSum / 128.0 < -0.2);
    }

#ifdef SICNU_HAS_OPENCV
    // 3. Supervised classification on the analysis-ready stack with a
    //    two-polygon training set over the same grid.
    const QString training = tmp.path() + QStringLiteral("/training.gpkg");
    {
        GDALDriverH drv = GDALGetDriverByName("GPKG");
        REQUIRE(drv != nullptr);
        GDALDatasetH ds = GDALCreate(drv, training.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
        REQUIRE(ds != nullptr);
        OGRLayerH lyr = GDALDatasetCreateLayer(ds, "training", nullptr, wkbPolygon, nullptr);
        REQUIRE(lyr != nullptr);
        OGRFieldDefnH fld = OGR_Fld_Create("class_id", OFTInteger);
        REQUIRE(OGR_L_CreateField(lyr, fld, true) == OGRERR_NONE);
        OGR_Fld_Destroy(fld);

        auto addPolygon = [&](int classId, double x0, double x1) {
            OGRGeometryH ring = OGR_G_CreateGeometry(wkbLinearRing);
            OGR_G_AddPoint_2D(ring, x0, 4399936.0);
            OGR_G_AddPoint_2D(ring, x1, 4399936.0);
            OGR_G_AddPoint_2D(ring, x1, 4400000.0);
            OGR_G_AddPoint_2D(ring, x0, 4400000.0);
            OGR_G_AddPoint_2D(ring, x0, 4399936.0);
            OGRGeometryH poly = OGR_G_CreateGeometry(wkbPolygon);
            REQUIRE(OGR_G_AddGeometryDirectly(poly, ring) == OGRERR_NONE);
            OGRFeatureH feat = OGR_F_Create(OGR_L_GetLayerDefn(lyr));
            OGR_F_SetFieldInteger(feat, 0, classId);
            REQUIRE(OGR_F_SetGeometryDirectly(feat, poly) == OGRERR_NONE);
            REQUIRE(OGR_L_CreateFeature(lyr, feat) == OGRERR_NONE);
            OGR_F_Destroy(feat);
        };
        addPolygon(1, 500000.0, 500064.0); // vegetation side
        addPolygon(2, 500064.0, 500128.0); // water side
        GDALClose(ds);
    }

    auto classifyOp = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(classifyOp != nullptr);
    const QString map = tmp.path() + QStringLiteral("/gf1_classified.tif");
    Json::Value classifyParams(Json::objectValue);
    classifyParams["input"] = stacked.toStdString();
    classifyParams["output"] = map.toStdString();
    classifyParams["training"] = training.toStdString();
    classifyParams["classField"] = "class_id";
    classifyParams["testSplit"] = 0.3;
    Json::Value classifyResult;
    REQUIRE_NOTHROW(classifyResult = classifyOp->run(classifyParams, ctx));
    REQUIRE(QFile::exists(map));
    REQUIRE(classifyResult["classes"].asInt() == 2);
    REQUIRE(classifyResult["overallAccuracy"].asDouble() == Catch::Approx(1.0).margin(1e-6));

    GdalDatasetWrapper mapDs;
    REQUIRE(mapDs.open(map));
    std::vector<float> mapPixels(16 * 16);
    REQUIRE(mapDs.readBandData(1, mapPixels.data(), 16, 16));
    // Statistics sanity: both classes present, left side is class of
    // vegetation, right side the other class.
    const float leftClass = mapPixels[0];
    const float rightClass = mapPixels[8];
    REQUIRE(leftClass != rightClass);
    for (int y = 0; y < 16; ++y) {
        REQUIRE(mapPixels[y * 16 + 0] == leftClass);
        REQUIRE(mapPixels[y * 16 + 15] == rightClass);
    }
#endif
}

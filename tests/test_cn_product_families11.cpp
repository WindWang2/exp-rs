// test_cn_product_families11.cpp — ADR 0159 CN product families (GF-3 SAR,
// GF-4 PMI, GF-5 AHSI, ZY-1 02B/02D/02E, CBERS-4 INPE generation):
// identity, sidecar generation separation, declared-metadata parsing,
// registry-backed band roles, hyperspectral band-axis aggregation and the
// refined diagnosable refusals.
//
// All fixtures are synthetic (hand-written XML + tiny GeoTIFFs); the golden
// expectations are literals derived from the fixture contents and the
// published band layouts recorded in data/products/sensor_profiles — never
// from the parser under test. No real imagery is committed anywhere.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include "geospatial/products/cn_product_adapters.h"
#include "geospatial/products/cn_product_metadata.h"
#include "geospatial/products/product_adapters.h"
#include "geospatial/products/product_registry.h"
#include "geospatial/products/sensor_profile.h"

#include <array>

using namespace sicnu::geo;
#include <cmath>
#include <string>
#include <vector>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

char appArgv0[] = "test_cn_product_families11";
int appArgc = 1;
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc, appArgv);
}

void pinRegistryToSource()
{
    // Hermetic: always resolve the COMMITTED registry, never a host override.
    qputenv("SICNU_DATA_DIR", (QString(CMAKE_SOURCE_DIR) + "/data").toUtf8());
}

void writeStackTiff(const QString &path, int bands, int width = 4)
{
    static bool gdalReady = false;
    if (!gdalReady) {
        GDALAllRegister();
        OGRRegisterAll();
        gdalReady = true;
    }
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);
    std::array<double, 6> gt = {500000, 8.0, 0, 4400000, 0, -8.0};
    const int height = 4;
    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), width, height, bands,
                                 GDT_Float32, nullptr);
    REQUIRE(ds != nullptr);
    GDALSetGeoTransform(ds, gt.data());
    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    OSRImportFromEPSG(srs, 32649);
    char *wkt = nullptr;
    OSRExportToWkt(srs, &wkt);
    GDALSetProjection(ds, wkt);
    CPLFree(wkt);
    OSRDestroySpatialReference(srs);
    std::vector<float> line(static_cast<size_t>(width), 1.0f);
    for (int b = 0; b < bands; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
        for (int row = 0; row < height; ++row)
            REQUIRE(GDALRasterIO(band, GF_Write, 0, row, width, 1, line.data(), width, 1,
                                 GDT_Float32, 0, 0) == CE_None);
    }
    GDALClose(ds);
}

void writeText(const QString &path, const QString &content)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content.toUtf8());
    file.close();
}

/// Legacy CRESDA <MetaInfo> sidecar (GF-3 style), with optional extras.
QString legacyMetaInfo(const QString &productId, const QString &satellite,
                       const QString &sensor, const QString &modeId,
                       const QStringList &extraTags = {})
{
    QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                "<MetaInfo>\n"
                                "  <ProductID>%1</ProductID>\n"
                                "  <SatelliteID>%2</SatelliteID>\n"
                                "  <SensorID>%3</SensorID>\n"
                                "  <ModeID>%4</ModeID>\n"
                                "  <ProductLevel>L1A</ProductLevel>\n"
                                "  <ReceiveDate>2022-11-21</ReceiveDate>\n"
                                "  <ReceiveTime>10:23:45</ReceiveTime>\n"
                                "  <OrbitID>08871</OrbitID>\n")
                      .arg(productId, satellite, sensor, modeId);
    for (const QString &tag : extraTags)
        xml += "  " + tag + "\n";
    xml += "</MetaInfo>\n";
    return xml;
}

TEST_CASE("cn11: GF-3 SAR is identified and parsed at declared-metadata level",
          "[cn11][gf3][identity]")
{
    ensureApp();
    pinRegistryToSource();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString base = tmp.path() + "/GF3_QPS_E113.9_N22.2_20221121_L1A00000012345-HH";
    writeStackTiff(base + ".tiff", 1);
    writeText(base + ".xml",
              legacyMetaInfo("31887", "GF3", "SAR", "QPS",
                             {
                                 "<PolarizationMode>HH/HV</PolarizationMode>",
                                 "<IncidenceAngle>31.47</IncidenceAngle>",
                                 "<BeamMode>QPS1</BeamMode>",
                                 "<PixelSizeX>3.0</PixelSizeX>",
                                 "<OrbitDirection>DESCENDING</OrbitDirection>",
                             }));

    const CnProductIdentity identity = cnIdentifyProduct((base + ".tiff").toStdString());
    REQUIRE(identity.recognized);
    REQUIRE(identity.supported);
    REQUIRE(identity.satellite == "GF3");
    REQUIRE(identity.kindName == "gaofen3_sar_product");
    REQUIRE(detectProductKind((base + ".tiff").toStdString()) == ProductKind::Gaofen3SarProduct);

    const ProductMetadata metadata = readCnProductMetadata((base + ".tiff").toStdString(), identity);
    REQUIRE(metadata.modality == "sar");
    REQUIRE(metadata.platform == "GF3");
    REQUIRE(metadata.processingLevel == "L1A");
    // f578db20c (#1230): SAR L1A stamps the SAR DN token "dn"; the calibrate
    // vocabulary guard accepts only "dn" and REFUSES the optical
    // "digital_number", so stamping optical here would smuggle an optical
    // product past rs:sar_calibrate. Golden fixture gf3_sar_valid.json pins
    // the same.
    REQUIRE(metadata.radiometricState == "dn");
    // Declared polarizations, canonical tokens in declared order.
    REQUIRE(metadata.polarizations.size() == 2);
    REQUIRE(metadata.polarizations[0] == "HH");
    REQUIRE(metadata.polarizations[1] == "HV");
    REQUIRE(metadata.orbitId == "08871");
    REQUIRE(metadata.orbitDirection == "DESCENDING");
    REQUIRE(metadata.hasResolution);
    REQUIRE(metadata.resolutionMeters == Catch::Approx(3.0));
    // Declared imaging geometry rides the bounded passthrough.
    bool sawIncidence = false;
    for (const auto &entry : metadata.extra)
        sawIncidence |= entry.first == "incidence_angle_deg" && entry.second == "31.47";
    REQUIRE(sawIncidence);
    REQUIRE(metadata.parseDiagnostics["generation"].asString() == "cresda_legacy_metainfo");

    // Registry projection: SAR intensity band carries no fabricated role.
    const CnBandRoleTable table = cnBandRoleTable("gf3");
    REQUIRE(table.bands.size() == 1);
    REQUIRE(table.bands.front().role == "unknown");
    REQUIRE_FALSE(table.bands.front().roleReason.empty());

    // Adapter enumeration claims the product.
    ProductAdapterRegistry &registry = ProductAdapterRegistry::instance();
    ProductAssets assets = registry.describe((base + ".tiff").toStdString());
    REQUIRE(assets.kind == ProductKind::Gaofen3SarProduct);
    REQUIRE(assets.completeness == ProductCompleteness::Complete);
    qunsetenv("SICNU_DATA_DIR");
}

TEST_CASE("cn11: GF-3 reports unknown polarization tokens instead of guessing",
          "[cn11][gf3][negative]")
{
    ensureApp();
    pinRegistryToSource();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString base = tmp.path() + "/GF3_FSAR_E113.0_N23.0_20230101_L1A00000077-HV";
    writeStackTiff(base + ".tiff", 1);
    writeText(base + ".xml",
              legacyMetaInfo("31888", "GF3", "SAR", "FSAR",
                             { "<PolarizationMode>QQ/HH</PolarizationMode>" }));

    const CnProductIdentity identity = cnIdentifyProduct((base + ".tiff").toStdString());
    REQUIRE(identity.supported);
    const ProductMetadata metadata = readCnProductMetadata((base + ".tiff").toStdString(), identity);
    REQUIRE(metadata.polarizations.size() == 1); // only the canonical HH channel
    REQUIRE(metadata.polarizations.front() == "HH");
    REQUIRE(metadata.parseDiagnostics["polarization_unknown_tokens"].size() == 1);
    REQUIRE(metadata.parseDiagnostics["polarization_unknown_tokens"][0].asString() == "QQ");
    qunsetenv("SICNU_DATA_DIR");
}

TEST_CASE("cn11: GF-3 with a non-CRESDA sidecar is refused, never guessed",
          "[cn11][gf3][negative]")
{
    ensureApp();
    pinRegistryToSource();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString base = tmp.path() + "/GF3_UFS_E113.0_N23.0_20230102_L1A00000078-HH";
    writeStackTiff(base + ".tiff", 1);
    writeText(base + ".xml", QStringLiteral("<?xml version=\"1.0\"?>\n"
                                           "<recipe><sugar>200g</sugar></recipe>\n"));

    const CnProductIdentity identity = cnIdentifyProduct((base + ".tiff").toStdString());
    REQUIRE(identity.supported);
    bool threw = false;
    try {
        (void)readCnProductMetadata((base + ".tiff").toStdString(), identity);
    } catch (const GeoError &error) {
        threw = true;
        REQUIRE(error.code() == sicnu::geo::ErrorCode::UnsupportedProduct);
        REQUIRE(std::string(error.what()).find("CRESDA") != std::string::npos);
    }
    REQUIRE(threw);
    qunsetenv("SICNU_DATA_DIR");
}

TEST_CASE("cn11: GF-4 PMI is identified; PAN/MS resolves through the registry link",
          "[cn11][gf4][identity]")
{
    ensureApp();
    pinRegistryToSource();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // Multispectral product: declared 4-band inventory.
    const QString msBase = tmp.path() + "/GF4_PMI_E113.0_N23.0_20230301_L1A00000021-MSS";
    writeStackTiff(msBase + ".tiff", 4);
    writeText(msBase + ".xml",
              legacyMetaInfo("40021", "GF4", "PMI", "PMS",
                             {
                                 "<BandID>B1</BandID><BandID>B2</BandID>"
                                 "<BandID>B3</BandID><BandID>B4</BandID>",
                                 "<PixelSizeX>50.0</PixelSizeX>",
                             }));
    const CnProductIdentity msIdentity = cnIdentifyProduct((msBase + ".tiff").toStdString());
    REQUIRE(msIdentity.supported);
    REQUIRE(msIdentity.kindName == "gaofen4_product");
    REQUIRE(detectProductKind((msBase + ".tiff").toStdString()) == ProductKind::Gaofen4Product);
    const ProductMetadata msMetadata = readCnProductMetadata((msBase + ".tiff").toStdString(), msIdentity);
    const std::string msKey = cnSensorKey(msIdentity, msMetadata);
    REQUIRE(msKey == "gf4_pmi");
    const CnBandRoleTable msTable = cnBandRoleTable(msKey);
    REQUIRE(msTable.bands.size() == 4);
    REQUIRE(msTable.bands[0].role == "blue");
    REQUIRE(msTable.bands[0].wavelengthNm == Catch::Approx(485.0));
    REQUIRE(msTable.bands[3].role == "nir");

    // Panchromatic product: 1-band inventory selects the pan sibling.
    const QString panBase = tmp.path() + "/GF4_PMI_E113.0_N23.0_20230301_L1A00000022-PAN";
    writeStackTiff(panBase + ".tiff", 1);
    writeText(panBase + ".xml",
              legacyMetaInfo("40022", "GF4", "PMI", "PMS",
                             { "<BandID>B1</BandID>", "<PixelSizeX>50.0</PixelSizeX>" }));
    const CnProductIdentity panIdentity = cnIdentifyProduct((panBase + ".tiff").toStdString());
    const ProductMetadata panMetadata = readCnProductMetadata((panBase + ".tiff").toStdString(), panIdentity);
    REQUIRE(cnSensorKey(panIdentity, panMetadata) == "gf4_pmi_pan");
    const CnBandRoleTable panTable = cnBandRoleTable("gf4_pmi_pan");
    REQUIRE(panTable.bands.front().role == "panchromatic");
    qunsetenv("SICNU_DATA_DIR");
}

TEST_CASE("cn11: GF-5 AHSI is identified and its band axis is aggregated",
          "[cn11][gf5][hyperspectral]")
{
    ensureApp();
    pinRegistryToSource();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const CnProductIdentity identity =
        cnIdentifyProduct("/data/GF5_AHSI_E113.5_N31.5_20230512_L1A0123456789.tiff");
    REQUIRE(identity.recognized);
    REQUIRE(identity.supported);
    REQUIRE(identity.kindName == "gaofen5_product");
    REQUIRE(detectProductKind("/data/GF5_AHSI_E113.5_N31.5_20230512_L1A0123456789.tiff") ==
            ProductKind::Gaofen5Product);

    // Registry axis: 330 declared bands, ordering documented, nothing invented.
    const SensorProfileRecord profile = loadSensorProfile("gf5_ahsi");
    REQUIRE(profile.modality == "hyperspectral");
    REQUIRE(profile.hasBandAxis);
    REQUIRE(profile.bandAxisCount == 330);
    REQUIRE(profile.bands.size() == 330);
    REQUIRE(profile.bands.front().band == "B1");
    REQUIRE(profile.bands.back().band == "B330");
    REQUIRE(profile.bands.front().role == "unknown");
    REQUIRE_FALSE(profile.bands.front().roleReason.empty());
    // No fabricated wavelengths anywhere on the axis.
    for (const SensorBandProfile &band : profile.bands)
        REQUIRE_FALSE(band.hasWavelengthNm);
    REQUIRE(profile.bandAxisOrdering.find("VNIR") != std::string::npos);
    REQUIRE(profile.bandAxisOrdering.find("SWIR") != std::string::npos);

    // Enumeration aggregates the declared inventory into ONE measurement asset.
    const QString base = tmp.path() + "/GF5_AHSI_E113.5_N31.5_20230512_L1A0123456789";
    writeStackTiff(base + ".tiff", 4); // fixture is small; the axis contract is registry-side
    QString bands;
    for (int i = 1; i <= 330; ++i)
        bands += (i > 1 ? "," : "") + QString::number(i);
    writeText(base + ".xml",
              legacyMetaInfo("51230", "GF5", "AHSI", "AHSI",
                             {
                                 QStringLiteral("<Bands>%1</Bands>").arg(bands),
                                 "<PixelSizeX>30.0</PixelSizeX>",
                             }));
    ProductAdapterRegistry &registry = ProductAdapterRegistry::instance();
    ProductAssets assets = registry.describe((base + ".tiff").toStdString());
    REQUIRE(assets.kind == ProductKind::Gaofen5Product);
    REQUIRE(assets.completeness == ProductCompleteness::Complete);
    const Json::Value axis = assets.notes["band_axis"];
    REQUIRE(axis.isObject());
    REQUIRE(axis["count"].asInt() == 330);
    REQUIRE(axis["declared_bands"].asInt() == 330);
    REQUIRE(QString::fromStdString(axis["ordering"].asString()).contains("VNIR"));
    // One measurement asset + one sidecar asset — not 330 rows.
    int measurements = 0;
    for (const ProductAsset &asset : assets.assets)
        measurements += asset.role == "measurement" ? 1 : 0;
    REQUIRE(measurements == 1);
    qunsetenv("SICNU_DATA_DIR");
}

TEST_CASE("cn11: ZY-1 02B/02D/02E are identified with registry-backed modes",
          "[cn11][zy1][identity]")
{
    ensureApp();
    pinRegistryToSource();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    struct Family
    {
        const char *fileName;
        const char *satellite;
        const char *kindSensorMode;
        const char *sensorKey;
        int expectedBandCount;
        const char *expectedFirstRole;
    };
    const Family families[] = {
        { "ZY1_02B_CCD_E113.0_N23.0_20150101_L1A0000001-MSS", "ZY1_02B", "CCD",
          "zy1_02b_ccd", 4, "blue" },
        { "ZY1_02B_HR_E113.0_N23.0_20150101_L1A0000002-PAN", "ZY1_02B", "HR",
          "zy1_02b_hr", 1, "panchromatic" },
        { "ZY1_02D_PMS_E113.0_N23.0_20210501_L1A0000003-MSS", "ZY1_02D", "PMS",
          "zy1_02d_pms", 4, "blue" },
        { "ZY1_02E_PMS_E113.0_N23.0_20230501_L1A0000004-MSS", "ZY1_02E", "PMS",
          "zy1_02e_pms", 4, "blue" },
    };
    for (const Family &family : families) {
        INFO("family: " << family.fileName);
        const CnProductIdentity identity = cnIdentifyProduct(std::string("/") + family.fileName + ".tiff");
        REQUIRE(identity.recognized);
        REQUIRE(identity.supported);
        REQUIRE(identity.satellite == family.satellite);
        REQUIRE(identity.sensorMode == family.kindSensorMode);
        REQUIRE(identity.kindName == "zy1_product");
        const CnBandRoleTable table = cnBandRoleTable(family.sensorKey);
        REQUIRE(static_cast<int>(table.bands.size()) == family.expectedBandCount);
        REQUIRE(table.bands.front().role == family.expectedFirstRole);
    }

    // ZY-1 02D AHSI axis: 166 bands, documented ordering, no fabricated values.
    const CnProductIdentity ahsi =
        cnIdentifyProduct("/data/ZY1_02D_AHSI_E113.0_N23.0_20210501_L1A0000005.tiff");
    REQUIRE(ahsi.supported);
    REQUIRE(ahsi.sensorKey == "zy1_02d_ahsi");
    const SensorProfileRecord profile = loadSensorProfile("zy1_02d_ahsi");
    REQUIRE(profile.hasBandAxis);
    REQUIRE(profile.bandAxisCount == 166);
    REQUIRE(profile.bands.size() == 166);
    REQUIRE(profile.bandAxisOrdering.find("B77..B166 SWIR") != std::string::npos);
    for (const SensorBandProfile &band : profile.bands)
        REQUIRE_FALSE(band.hasWavelengthNm);

    // 02D PMS pan variant selection from a 1-band inventory.
    const QString panBase = tmp.path() + "/ZY1_02D_PMS_E113.0_N23.0_20210501_L1A0000006-PAN";
    writeStackTiff(panBase + ".tiff", 1);
    writeText(panBase + ".xml",
              legacyMetaInfo("21230", "ZY1_02D", "PMS", "PMS", { "<BandID>B1</BandID>" }));
    const CnProductIdentity panIdentity = cnIdentifyProduct((panBase + ".tiff").toStdString());
    const ProductMetadata panMetadata = readCnProductMetadata((panBase + ".tiff").toStdString(), panIdentity);
    REQUIRE(cnSensorKey(panIdentity, panMetadata) == "zy1_02d_pms_pan");
    qunsetenv("SICNU_DATA_DIR");
}

TEST_CASE("cn11: CBERS-4 is parsed in the INPE sidecar generation, schema never mixes",
          "[cn11][cbers][identity]")
{
    ensureApp();
    pinRegistryToSource();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString base = tmp.path() + "/CBERS4_MUX_20230415_0364_075_L1.tiff";
    writeStackTiff(base + ".tiff", 4);
    writeText(base + ".xml",
              QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                             "<metadata>\n"
                             "  <productid>CBERS4_MUX_20230415_0364_075_L1</productid>\n"
                             "  <satellite>CBERS4</satellite>\n"
                             "  <sensor>MUX</sensor>\n"
                             "  <resolution>20.0</resolution>\n"
                             "  <acquisitiondate>2023-04-15</acquisitiondate>\n"
                             "  <acquisitiontime>13:45:30</acquisitiontime>\n"
                             "  <orbit>1234</orbit>\n"
                             "  <path>364</path>\n"
                             "  <row>75</row>\n"
                             "  <solarzenith>32.5</solarzenith>\n"
                             "  <sunazimuth>154.2</sunazimuth>\n"
                             "  <projection>UTM</projection>\n"
                             "  <utmzone>22</utmzone>\n"
                             "  <bands>1,2,3,4</bands>\n"
                             "</metadata>\n"));

    const CnProductIdentity identity = cnIdentifyProduct((base + ".tiff").toStdString());
    REQUIRE(identity.recognized);
    REQUIRE(identity.supported);
    REQUIRE(identity.satellite == "CBERS4");
    REQUIRE(identity.kindName == "cbers_product");
    REQUIRE(detectProductKind((base + ".tiff").toStdString()) == ProductKind::CbersProduct);

    const ProductMetadata metadata = readCnProductMetadata((base + ".tiff").toStdString(), identity);
    REQUIRE(metadata.platform == "CBERS4");
    REQUIRE(metadata.sensor == "MUX");
    REQUIRE(metadata.modality == "optical");
    REQUIRE(metadata.hasResolution);
    REQUIRE(metadata.resolutionMeters == Catch::Approx(20.0));
    REQUIRE(metadata.acquisitionTime == "2023-04-15T13:45:30");
    REQUIRE(metadata.orbitId == "1234");
    // Zenith-derived elevation carries its derivation, never a naked value.
    REQUIRE(metadata.hasSunElevation);
    REQUIRE(metadata.sunElevationDeg == Catch::Approx(57.5).margin(1e-9));
    REQUIRE(metadata.sunElevationSource.find("90 - declared solar zenith") != std::string::npos);
    REQUIRE(metadata.hasSunAzimuth);
    REQUIRE(metadata.sunAzimuthDeg == Catch::Approx(154.2));
    REQUIRE(metadata.crsHint == "UTM zone 22");
    REQUIRE(metadata.declaredBandIds.size() == 4);
    REQUIRE(metadata.declaredBandIds[0] == "B1");
    REQUIRE(metadata.declaredBandIds[3] == "B4");
    bool sawPathRow = false;
    for (const auto &entry : metadata.extra)
        sawPathRow |= entry.first == "path_row" && entry.second == "364/75";
    REQUIRE(sawPathRow);
    REQUIRE(metadata.parseDiagnostics["generation"].asString() == "cbers_inpe_metadata");
    REQUIRE(metadata.parseDiagnostics["root_element"].asString() == "metadata");

    // Registry-backed roles for the MUX camera.
    const CnBandRoleTable table = cnBandRoleTable("cbers4_mux");
    REQUIRE(table.bands.size() == 4);
    REQUIRE(table.bands[0].role == "blue");
    REQUIRE(table.bands[3].role == "nir");

    // Unknown CBERS sidecar generation: refused, never guessed.
    const QString badBase = tmp.path() + "/CBERS4_WFI_20230415_0364_075_L2.tiff";
    writeStackTiff(badBase + ".tiff", 4);
    writeText(badBase + ".xml", QStringLiteral("<?xml version=\"1.0\"?>\n"
                                              "<warehouse><crate>17</crate></warehouse>\n"));
    const CnProductIdentity badIdentity = cnIdentifyProduct((badBase + ".tiff").toStdString());
    REQUIRE(badIdentity.supported); // WFI is an adapted camera…
    bool threw = false;
    try {
        (void)readCnProductMetadata((badBase + ".tiff").toStdString(), badIdentity);
    } catch (const GeoError &error) {
        threw = true;
        REQUIRE(error.code() == sicnu::geo::ErrorCode::UnsupportedProduct);
        REQUIRE(std::string(error.what()).find("Unknown CBERS sidecar generation") !=
                std::string::npos);
    }
    REQUIRE(threw);
    qunsetenv("SICNU_DATA_DIR");
}

TEST_CASE("cn11: unadapted CN sub-modes keep concrete refusal reasons",
          "[cn11][refusals]")
{
    ensureApp();
    pinRegistryToSource();
    struct Case
    {
        const char *path;
        const char *reasonFragment;
    };
    const Case cases[] = {
        { "/data/GF4_IRC_E113.0_N23.0_20230301_L1A00000099.tiff", "only GF-4 PMI" },
        { "/data/GF4_PMS_E113.0_N23.0_20230301_L1A00000098.tiff", "only GF-4 PMI" },
        { "/data/GF5_VIMS_E113.0_N23.0_20230512_L1A00000097.tiff", "only GF-5 AHSI" },
        { "/data/GF5_GMI_E113.0_N23.0_20230512_L1A00000096.tiff", "only GF-5 AHSI" },
        { "/data/ZY1_02D_IRS_E113.0_N23.0_20210501_L1A00000095.tiff", "02D/02E PMS/AHSI" },
        { "/data/ZY5_VMS_E113.0_N23.0_20210501_L1A00000094.tiff", "ZY-5 are not" },
        { "/data/CBERS4_ERM_20230415_0364_075_L1.tiff", "other CBERS missions" },
        { "/data/CBERS1_MUX_20230415_0364_075_L1.tiff", "other CBERS missions" },
    };
    for (const Case &testCase : cases) {
        INFO("path: " << testCase.path);
        const CnProductIdentity identity = cnIdentifyProduct(testCase.path);
        REQUIRE(identity.recognized);
        REQUIRE_FALSE(identity.supported);
        REQUIRE(std::string(identity.reason).find(testCase.reasonFragment) != std::string::npos);
    }
    qunsetenv("SICNU_DATA_DIR");
}

TEST_CASE("cn11: registry drift gate still holds with the new families committed",
          "[cn11][registry][drift]")
{
    ensureApp();
    pinRegistryToSource();
    const std::vector<SensorProfileValidationIssue> issues = validateSensorProfiles();
    for (const SensorProfileValidationIssue &issue : issues)
        FAIL(issue.file + "/" + issue.sensorKey + (issue.band.empty() ? "" : "/" + issue.band) +
             ": " + issue.message);
    REQUIRE(issues.empty());
    qunsetenv("SICNU_DATA_DIR");
}

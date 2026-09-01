// test_satellite_products.cpp — Landsat / Sentinel-2 / MODIS discovery & import
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <gdal.h>
#include <gdal_priv.h>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_landsat_import_operator.h"
#include "operators/rs/rs_sentinel2_import_operator.h"
#include "operators/rs/rs_modis_import_operator.h"
#include "operators/rs/rs_modis_georeference_operator.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace sicnu::operators;

namespace {

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_satellite_products";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

void writeTinyBand(const QString& path, float fill)
{
    ensureGdalInit();
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    std::vector<std::vector<float>> bands(1, std::vector<float>(4 * 4, fill));
    QString err;
    REQUIRE(writeGdalOutput(path, 4, 4, bands, gt,
                            QStringLiteral("EPSG:32648"), &err));
}

QString writeFakeLandsatScene(const QDir& root,
                              const QString& spacecraft = QStringLiteral("LANDSAT_8"),
                              const QString& level = QStringLiteral("L1TP"),
                              const QStringList& extraMtlLines = {})
{
    const QString scene = root.filePath(QStringLiteral("LC08_L1TP_TEST"));
    QDir().mkpath(scene);
    QDir sdir(scene);

    // Minimal MTL
    QFile mtl(sdir.filePath(QStringLiteral("LC08_L1TP_TEST_MTL.txt")));
    REQUIRE(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&mtl);
    out << "GROUP = LANDSAT_METADATA_FILE\n";
    out << "  SPACECRAFT_ID = \"" << spacecraft << "\"\n";
    out << "  PROCESSING_LEVEL = \"" << level << "\"\n";
    out << "  DATE_ACQUIRED = \"2020-06-15\"\n";
    out << "  LANDSAT_PRODUCT_ID = \"LC08_L1TP_TEST\"\n";
    out << "  FILE_NAME_BAND_2 = \"LC08_L1TP_TEST_B2.TIF\"\n";
    out << "  FILE_NAME_BAND_3 = \"LC08_L1TP_CLASS_B3.TIF\"\n";
    out << "  FILE_NAME_BAND_4 = \"LC08_L1TP_CLASS_B4.TIF\"\n";
    out << "  FILE_NAME_BAND_5 = \"LC08_L1TP_CLASS_B5.TIF\"\n";
    for (const QString& line : extraMtlLines)
        out << "  " << line << "\n";
    out << "END_GROUP = LANDSAT_METADATA_FILE\n";
    out << "END\n";
    mtl.close();

    writeTinyBand(sdir.filePath(QStringLiteral("LC08_L1TP_CLASS_B2.TIF")), 100.f);
    writeTinyBand(sdir.filePath(QStringLiteral("LC08_L1TP_CLASS_B3.TIF")), 120.f);
    writeTinyBand(sdir.filePath(QStringLiteral("LC08_L1TP_CLASS_B4.TIF")), 80.f);
    writeTinyBand(sdir.filePath(QStringLiteral("LC08_L1TP_CLASS_B5.TIF")), 200.f);
    return mtl.fileName();
}

QString writeFakeSentinel2Safe(const QDir& root)
{
    const QString safe = root.filePath(
        QStringLiteral("S2A_MSIL2A_20200615T000000_N9999_R000_T32TQQ_20200615T000000.SAFE"));
    const QString img = safe + QStringLiteral("/GRANULE/L2A_T32TQQ/IMG_DATA/R10m");
    QDir().mkpath(img);

    QFile mtd(QDir(safe).filePath(QStringLiteral("MTD_MSIL2A.xml")));
    REQUIRE(mtd.open(QIODevice::WriteOnly | QIODevice::Text));
    mtd.write("<n1:Level-2A_User_Product></n1:Level-2A_User_Product>\n");
    mtd.close();

    // Use .tif instead of jp2 so GDAL always opens without JP2 driver
    writeTinyBand(img + QStringLiteral("/T32TQQ_20200615T000000_B02_10m.tif"), 50.f);
    writeTinyBand(img + QStringLiteral("/T32TQQ_20200615T000000_B03_10m.tif"), 60.f);
    writeTinyBand(img + QStringLiteral("/T32TQQ_20200615T000000_B04_10m.tif"), 40.f);
    writeTinyBand(img + QStringLiteral("/T32TQQ_20200615T000000_B08_10m.tif"), 180.f);
    // L2A auxiliary layers live at their native resolution (SCL is 20 m) and
    // are discovered regardless of the preferred optical resolution.
    const QString img20 = safe + QStringLiteral("/GRANULE/L2A_T32TQQ/IMG_DATA/R20m");
    QDir().mkpath(img20);
    writeTinyBand(img20 + QStringLiteral("/T32TQQ_20200615T000000_SCL_20m.tif"), 4.f);
    return safe;
}

/** Synthetic unreferenced multi-band GeoTIFF with MODIS product filename (h27v06). */
QString writeFakeModisTile(const QDir& root, int width = 12, int height = 12)
{
    ensureGdalInit();
    const QString path = root.filePath(
        QStringLiteral("MOD09GQ.A2020161.h27v06.061.2020163012345.tif"));

    // Unreferenced raster (identity-like GT, empty CRS) — mimics HDF export without geo.
    std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
    std::vector<std::vector<float>> bands;
    for (int b = 0; b < 3; ++b) {
        bands.emplace_back(static_cast<size_t>(width * height), 100.f + 10.f * b);
    }
    QString err;
    REQUIRE(writeGdalOutput(path, width, height, bands, gt, QString(), &err));

    // Name bands like MODIS surface reflectance
    GDALDatasetH ds = GDALOpen(path.toUtf8().constData(), GA_Update);
    REQUIRE(ds != nullptr);
    const char* names[] = {"sur_refl_b01", "sur_refl_b02", "sur_refl_b03"};
    for (int i = 0; i < 3; ++i) {
        GDALRasterBandH band = GDALGetRasterBand(ds, i + 1);
        REQUIRE(band != nullptr);
        GDALSetDescription(band, names[i]);
    }
    // Clear projection again (writeGdalOutput may leave empty string)
    GDALSetProjection(ds, "");
    GDALClose(ds);
    return path;
}

} // namespace

TEST_CASE("Landsat MTL discovery", "[satellite][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString mtl = writeFakeLandsatScene(QDir(tmp.path()));

    SatelliteProducts::ProductInfo info;
    QString err;
    REQUIRE(SatelliteProducts::discoverLandsat(mtl, &info, &err));
    REQUIRE(info.type == SatelliteProducts::ProductType::Landsat);
    REQUIRE(info.spacecraft.contains(QStringLiteral("LANDSAT")));
    REQUIRE(info.bands.size() >= 3);
    REQUIRE(info.productId.contains(QStringLiteral("LC08")));
    // Named optical bands from MTL FILE_NAME_BAND_* entries
    bool hasRed = false;
    bool hasNir = false;
    for (const auto& b : info.bands) {
        if (b.name == QStringLiteral("B4"))
            hasRed = true;
        if (b.name == QStringLiteral("B5"))
            hasNir = true;
    }
    REQUIRE(hasRed);
    REQUIRE(hasNir);
    // Semantic roles follow the OLI layout (SPACECRAFT_ID = LANDSAT_8).
    for (const auto& b : info.bands) {
        if (b.name == QStringLiteral("B2"))
            CHECK(b.role == sicnu::data::BandRole::Blue);
        if (b.name == QStringLiteral("B4"))
            CHECK(b.role == sicnu::data::BandRole::Red);
        if (b.name == QStringLiteral("B5"))
            CHECK(b.role == sicnu::data::BandRole::NIR);
    }
}

TEST_CASE("Sentinel-2 SAFE discovery", "[satellite][sentinel2]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString safe = writeFakeSentinel2Safe(QDir(tmp.path()));

    SatelliteProducts::ProductInfo info;
    QString err;
    REQUIRE(SatelliteProducts::discoverSentinel2(safe, &info, QStringLiteral("10m"), &err));
    REQUIRE(info.type == SatelliteProducts::ProductType::Sentinel2);
    REQUIRE(info.processingLevel == QStringLiteral("L2A"));
    REQUIRE(info.bands.size() >= 4);
    // Semantic roles from the MSI layout.
    for (const auto& b : info.bands) {
        if (b.name == QStringLiteral("B2"))
            CHECK(b.role == sicnu::data::BandRole::Blue);
        if (b.name == QStringLiteral("B4"))
            CHECK(b.role == sicnu::data::BandRole::Red);
        if (b.name == QStringLiteral("B8"))
            CHECK(b.role == sicnu::data::BandRole::NIR);
    }
    // The L2A Scene Classification Layer is discovered regardless of the
    // preferred optical resolution, with the SceneClassification role.
    bool hasScl = false;
    bool sclRole = false;
    for (const auto& b : info.bands) {
        if (b.name == QStringLiteral("SCL")) {
            hasScl = true;
            sclRole = b.role == sicnu::data::BandRole::SceneClassification;
        }
    }
    REQUIRE(hasScl);
    CHECK(sclRole);
}

TEST_CASE("Landsat stack to GeoTIFF", "[satellite][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString mtl = writeFakeLandsatScene(QDir(tmp.path()));
    const QString out = tmp.filePath(QStringLiteral("landsat_stack.tif"));

    SatelliteProducts::ProductInfo info;
    REQUIRE(SatelliteProducts::discoverLandsat(mtl, &info));
    QString err;
    REQUIRE(SatelliteProducts::stackToGeoTiff(
        info, {QStringLiteral("B4"), QStringLiteral("B5")}, out, &err));
    REQUIRE(QFileInfo::exists(out));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(out));
    REQUIRE(ds.bandCount() == 2);
    REQUIRE(ds.width() == 4);
    REQUIRE(ds.height() == 4);
}

TEST_CASE("rs:landsat_import operator", "[operators][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString mtl = writeFakeLandsatScene(QDir(tmp.path()));
    const QString out = tmp.filePath(QStringLiteral("ls_op.tif"));

    auto op = RSOperatorRegistry::instance().create("rs:landsat_import");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = mtl.toStdString();
    params["output"] = out.toStdString();
    params["bands"] = Json::Value(Json::arrayValue);
    params["bands"].append("B2");
    params["bands"].append("B3");
    params["bands"].append("B4");
    params["bands"].append("B5");

    RSOperatorContext ctx;
    Json::Value result = op->execute(params, ctx);
    REQUIRE(result["output"].asString() == out.toStdString());
    REQUIRE(result["bandCount"].asInt() == 4);
    REQUIRE(QFileInfo::exists(out));
}

TEST_CASE("rs:sentinel2_import operator", "[operators][sentinel2]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString safe = writeFakeSentinel2Safe(QDir(tmp.path()));
    const QString out = tmp.filePath(QStringLiteral("s2_op.tif"));

    auto op = RSOperatorRegistry::instance().create("rs:sentinel2_import");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = safe.toStdString();
    params["output"] = out.toStdString();
    params["resolution"] = "10m";

    RSOperatorContext ctx;
    Json::Value result = op->execute(params, ctx);
    REQUIRE(result["output"].asString() == out.toStdString());
    REQUIRE(result["bandCount"].asInt() == 4);
    REQUIRE(result["resolution"].asString() == "10m");
    REQUIRE(QFileInfo::exists(out));
}

TEST_CASE("MODIS tile index parse and geotransform", "[satellite][modis]")
{
    ensureApp();
    int h = -1, v = -1;
    REQUIRE(SatelliteProducts::parseModisTileIndices(
        QStringLiteral("MOD09GQ.A2020161.h27v06.061.hdf"), &h, &v));
    REQUIRE(h == 27);
    REQUIRE(v == 6);
    REQUIRE_FALSE(SatelliteProducts::parseModisTileIndices(
        QStringLiteral("not_a_modis_name.tif"), &h, &v));

    std::array<double, 6> gt{};
    QString err;
    REQUIRE(SatelliteProducts::modisTileGeoTransform(27, 6, 1200, 1200, &gt, &err));
    // NASA constants: XMin + h*tileSize, YMax - v*tileSize
    constexpr double kTile = 1111950.5196666666;
    constexpr double kXMin = -20015109.354;
    constexpr double kYMax = 10007554.677;
    REQUIRE_THAT(gt[0], Catch::Matchers::WithinAbs(kXMin + 27 * kTile, 1e-3));
    REQUIRE_THAT(gt[3], Catch::Matchers::WithinAbs(kYMax - 6 * kTile, 1e-3));
    REQUIRE_THAT(gt[1], Catch::Matchers::WithinAbs(kTile / 1200.0, 1e-6));
    REQUIRE(gt[5] < 0.0);
    REQUIRE(SatelliteProducts::modisSinusoidalWkt().contains(QStringLiteral("Sinusoidal")));
}

TEST_CASE("MODIS GeoTIFF discovery and stack with sinusoidal georef", "[satellite][modis]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString modis = writeFakeModisTile(QDir(tmp.path()));
    const QString out = tmp.filePath(QStringLiteral("modis_stack.tif"));

    SatelliteProducts::ProductInfo info;
    QString err;
    REQUIRE(SatelliteProducts::discoverModis(modis, &info, &err));
    REQUIRE(info.type == SatelliteProducts::ProductType::Modis);
    REQUIRE(info.spacecraft == QStringLiteral("Terra"));
    REQUIRE(info.modisTileH == 27);
    REQUIRE(info.modisTileV == 6);
    REQUIRE(info.bands.size() >= 3);
    // MODIS land band roles (sur_refl_b01 = Red, b02 = NIR).
    for (const auto& b : info.bands) {
        if (b.name == QStringLiteral("sur_refl_b01"))
            CHECK(b.role == sicnu::data::BandRole::Red);
        if (b.name == QStringLiteral("sur_refl_b02"))
            CHECK(b.role == sicnu::data::BandRole::NIR);
    }
    REQUIRE(info.acquisitionDate.contains(QStringLiteral("2020")));

    REQUIRE(SatelliteProducts::stackToGeoTiff(
        info,
        {QStringLiteral("sur_refl_b01"), QStringLiteral("sur_refl_b02")},
        out, &err));
    REQUIRE(QFileInfo::exists(out));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(out));
    REQUIRE(ds.bandCount() == 2);
    REQUIRE(ds.width() == 12);
    const auto gt = ds.geoTransform();
    REQUIRE(std::abs(gt[0]) > 1.0); // not identity origin
    REQUIRE(ds.projection().contains(QStringLiteral("Sinusoidal"), Qt::CaseInsensitive));
}

TEST_CASE("MODIS assign sinusoidal and reproject to EPSG:4326", "[satellite][modis]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString modis = writeFakeModisTile(QDir(tmp.path()), 24, 24);
    const QString sinu = tmp.filePath(QStringLiteral("modis_sinu.tif"));
    const QString wgs = tmp.filePath(QStringLiteral("modis_wgs84.tif"));

    QString err;
    REQUIRE(SatelliteProducts::assignModisSinusoidalGeoref(modis, sinu, -1, -1, &err));
    GdalDatasetWrapper dsSinu;
    REQUIRE(dsSinu.open(sinu));
    REQUIRE(dsSinu.projection().contains(QStringLiteral("Sinusoidal"), Qt::CaseInsensitive));

    REQUIRE(SatelliteProducts::georeferenceModis(
        modis, wgs, QStringLiteral("EPSG:4326"), -1, -1, QStringLiteral("bilinear"), &err));
    REQUIRE(QFileInfo::exists(wgs));
    GdalDatasetWrapper dsWgs;
    REQUIRE(dsWgs.open(wgs));
    // WGS84 / geographic CRS
    const QString proj = dsWgs.projection();
    REQUIRE((proj.contains(QStringLiteral("4326"))
             || proj.contains(QStringLiteral("WGS 84"), Qt::CaseInsensitive)
             || proj.contains(QStringLiteral("WGS84"), Qt::CaseInsensitive)
             || proj.contains(QStringLiteral("Geographic"), Qt::CaseInsensitive)));
    // Longitudes should land near East Asia for h27v06 (roughly China)
    const auto gt = dsWgs.geoTransform();
    REQUIRE(gt[0] > 90.0);
    REQUIRE(gt[0] < 140.0);
}

TEST_CASE("rs:modis_import and rs:modis_georeference operators", "[operators][modis]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString modis = writeFakeModisTile(QDir(tmp.path()));
    const QString stacked = tmp.filePath(QStringLiteral("modis_op_stack.tif"));
    const QString geo = tmp.filePath(QStringLiteral("modis_op_geo.tif"));

    auto importOp = RSOperatorRegistry::instance().create("rs:modis_import");
    REQUIRE(importOp != nullptr);

    Json::Value ip(Json::objectValue);
    ip["input"] = modis.toStdString();
    ip["output"] = stacked.toStdString();
    ip["bands"] = Json::Value(Json::arrayValue);
    ip["bands"].append("sur_refl_b01");
    ip["bands"].append("sur_refl_b02");

    RSOperatorContext ctx;
    Json::Value ir = importOp->execute(ip, ctx);
    REQUIRE(ir["output"].asString() == stacked.toStdString());
    REQUIRE(ir["tileH"].asInt() == 27);
    REQUIRE(ir["tileV"].asInt() == 6);
    REQUIRE(QFileInfo::exists(stacked));

    auto geoOp = RSOperatorRegistry::instance().create("rs:modis_georeference");
    REQUIRE(geoOp != nullptr);

    // Georeference from the original unreferenced product path (filename has h27v06).
    // Stacked output already carries sinusoidal CRS from import; this path also tests
    // filename tile parse + warp to WGS84.
    Json::Value gp(Json::objectValue);
    gp["input"] = modis.toStdString();
    gp["output"] = geo.toStdString();
    gp["dstCrs"] = "EPSG:4326";
    gp["resampling"] = "nearest";

    RSOperatorContext ctx2;
    Json::Value gr = geoOp->execute(gp, ctx2);
    REQUIRE(gr["output"].asString() == geo.toStdString());
    REQUIRE(gr["tileH"].asInt() == 27);
    REQUIRE(gr["tileV"].asInt() == 6);
    REQUIRE(QFileInfo::exists(geo));
}

TEST_CASE("Band role tables (Landsat OLI/legacy, Sentinel-2, MODIS)", "[satellite][roles]")
{
    // Landsat OLI (8/9): B1 Coastal, B2 Blue, B3 Green, B4 Red, B5 NIR,
    // B6 SWIR1, B7 SWIR2, B8 Pan, B9 Cirrus, B10/B11 Thermal, QA bands.
    CHECK(SatelliteProducts::landsatBandRole("B1", "LANDSAT_8")
          == sicnu::data::BandRole::Coastal);
    CHECK(SatelliteProducts::landsatBandRole("B2", "LANDSAT_8")
          == sicnu::data::BandRole::Blue);
    CHECK(SatelliteProducts::landsatBandRole("B3", "LANDSAT_8")
          == sicnu::data::BandRole::Green);
    CHECK(SatelliteProducts::landsatBandRole("B4", "LANDSAT_8")
          == sicnu::data::BandRole::Red);
    CHECK(SatelliteProducts::landsatBandRole("B5", "LANDSAT_8")
          == sicnu::data::BandRole::NIR);
    CHECK(SatelliteProducts::landsatBandRole("B6", "LANDSAT_9")
          == sicnu::data::BandRole::SWIR1);
    CHECK(SatelliteProducts::landsatBandRole("B7", "LANDSAT_9")
          == sicnu::data::BandRole::SWIR2);
    CHECK(SatelliteProducts::landsatBandRole("B8", "LANDSAT_8")
          == sicnu::data::BandRole::Panchromatic);
    CHECK(SatelliteProducts::landsatBandRole("B9", "LANDSAT_8")
          == sicnu::data::BandRole::Cirrus);
    CHECK(SatelliteProducts::landsatBandRole("B10", "LANDSAT_8")
          == sicnu::data::BandRole::Thermal);
    CHECK(SatelliteProducts::landsatBandRole("QA_PIXEL", "LANDSAT_8")
          == sicnu::data::BandRole::QA);
    // Legacy TM/ETM (4-7): B1 Blue, B6 Thermal, B7 SWIR2.
    CHECK(SatelliteProducts::landsatBandRole("B1", "LANDSAT_5")
          == sicnu::data::BandRole::Blue);
    CHECK(SatelliteProducts::landsatBandRole("B4", "LANDSAT_7")
          == sicnu::data::BandRole::NIR);
    CHECK(SatelliteProducts::landsatBandRole("B6", "LANDSAT_7")
          == sicnu::data::BandRole::Thermal);
    CHECK(SatelliteProducts::landsatBandRole("B7", "LANDSAT_5")
          == sicnu::data::BandRole::SWIR2);

    // Sentinel-2 MSI.
    CHECK(SatelliteProducts::sentinel2BandRole("B1") == sicnu::data::BandRole::Coastal);
    CHECK(SatelliteProducts::sentinel2BandRole("B2") == sicnu::data::BandRole::Blue);
    CHECK(SatelliteProducts::sentinel2BandRole("B3") == sicnu::data::BandRole::Green);
    CHECK(SatelliteProducts::sentinel2BandRole("B4") == sicnu::data::BandRole::Red);
    CHECK(SatelliteProducts::sentinel2BandRole("B5") == sicnu::data::BandRole::RedEdge);
    CHECK(SatelliteProducts::sentinel2BandRole("B7") == sicnu::data::BandRole::RedEdge);
    CHECK(SatelliteProducts::sentinel2BandRole("B8") == sicnu::data::BandRole::NIR);
    CHECK(SatelliteProducts::sentinel2BandRole("B8A") == sicnu::data::BandRole::NarrowNIR);
    CHECK(SatelliteProducts::sentinel2BandRole("B10") == sicnu::data::BandRole::Cirrus);
    CHECK(SatelliteProducts::sentinel2BandRole("B11") == sicnu::data::BandRole::SWIR1);
    CHECK(SatelliteProducts::sentinel2BandRole("B12") == sicnu::data::BandRole::SWIR2);
    CHECK(SatelliteProducts::sentinel2BandRole("SCL")
          == sicnu::data::BandRole::SceneClassification);
    CHECK(SatelliteProducts::sentinel2BandRole("MSK_CLDPRB_20m")
          == sicnu::data::BandRole::QA);
    CHECK(SatelliteProducts::sentinel2BandRole("B9") == sicnu::data::BandRole::Unknown);

    // MODIS land bands.
    CHECK(SatelliteProducts::modisBandRole("sur_refl_b01") == sicnu::data::BandRole::Red);
    CHECK(SatelliteProducts::modisBandRole("sur_refl_b02") == sicnu::data::BandRole::NIR);
    CHECK(SatelliteProducts::modisBandRole("sur_refl_b03") == sicnu::data::BandRole::Blue);
    CHECK(SatelliteProducts::modisBandRole("sur_refl_b04") == sicnu::data::BandRole::Green);
    CHECK(SatelliteProducts::modisBandRole("sur_refl_b05") == sicnu::data::BandRole::SWIR1);
    CHECK(SatelliteProducts::modisBandRole("sur_refl_b06") == sicnu::data::BandRole::SWIR2);
    CHECK(SatelliteProducts::modisBandRole("something_else") == sicnu::data::BandRole::Unknown);

    // Identifier round-trips are case-insensitive and stable.
    CHECK(sicnu::data::bandRoleFromString(
              sicnu::data::bandRoleToString(sicnu::data::BandRole::RedEdge))
          == sicnu::data::BandRole::RedEdge);
    CHECK(sicnu::data::bandRoleFromString("RED_EDGE") == sicnu::data::BandRole::RedEdge);
    CHECK(sicnu::data::bandRoleFromString("nope") == sicnu::data::BandRole::Unknown);
    CHECK(sicnu::data::bandRoleToString(sicnu::data::BandRole::NarrowNIR)
          == QStringLiteral("narrow_nir"));
}

TEST_CASE("Landsat stack writes band role and FWHM metadata", "[satellite][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString mtl = writeFakeLandsatScene(QDir(tmp.path()));
    const QString out = tmp.filePath(QStringLiteral("landsat_roles.tif"));

    SatelliteProducts::ProductInfo info;
    REQUIRE(SatelliteProducts::discoverLandsat(mtl, &info));
    QString err;
    REQUIRE(SatelliteProducts::stackToGeoTiff(
        info, {QStringLiteral("B4"), QStringLiteral("B5")}, out, &err));

    GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);
    GDALRasterBandH red = GDALGetRasterBand(ds, 1);
    GDALRasterBandH nir = GDALGetRasterBand(ds, 2);
    REQUIRE(red != nullptr);
    REQUIRE(nir != nullptr);

    const char* redRole = GDALGetMetadataItem(red, "SICNU_BAND_ROLE", nullptr);
    const char* nirRole = GDALGetMetadataItem(nir, "SICNU_BAND_ROLE", nullptr);
    REQUIRE(redRole != nullptr);
    REQUIRE(nirRole != nullptr);
    CHECK(QString::fromUtf8(redRole) == QStringLiteral("red"));
    CHECK(QString::fromUtf8(nirRole) == QStringLiteral("nir"));

    // OLI FWHM: B4 = 30 nm, B5 = 77 nm.
    const char* redFwhm = GDALGetMetadataItem(red, "FWHM", nullptr);
    const char* nirFwhm = GDALGetMetadataItem(nir, "FWHM", nullptr);
    REQUIRE(redFwhm != nullptr);
    REQUIRE(nirFwhm != nullptr);
    CHECK(QString::fromUtf8(redFwhm) == QStringLiteral("30"));
    CHECK(QString::fromUtf8(nirFwhm) == QStringLiteral("77"));
    GDALClose(ds);
}

// ---------------------------------------------------------------------------
// #673: Landsat 4-7 must use the TM/ETM+ centre-wavelength table
// ---------------------------------------------------------------------------

TEST_CASE("Landsat 4-7 discovery stamps TM/ETM+ wavelengths (#673)", "[satellite][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString mtl = writeFakeLandsatScene(QDir(tmp.path()),
                                              QStringLiteral("LANDSAT_5"),
                                              QStringLiteral("L1TP"));

    SatelliteProducts::ProductInfo info;
    QString err;
    REQUIRE(SatelliteProducts::discoverLandsat(mtl, &info, &err));
    bool sawB1 = false;
    bool sawB4 = false;
    bool sawB5 = false;
    for (const auto& b : info.bands) {
        if (b.name == QStringLiteral("B1")) {
            sawB1 = true;
            CHECK(b.wavelengthNm == 485); // TM Blue, not OLI Coastal 443
        }
        if (b.name == QStringLiteral("B4")) {
            sawB4 = true;
            CHECK(b.wavelengthNm == 830); // TM NIR
        }
        if (b.name == QStringLiteral("B5")) {
            sawB5 = true;
            CHECK(b.wavelengthNm == 1650); // TM SWIR1, not OLI NIR 865
        }
    }
    REQUIRE(sawB1);
    REQUIRE(sawB4);
    REQUIRE(sawB5);

    // The OLI table still applies to Landsat 8/9.
    QTemporaryDir tmpOli;
    REQUIRE(tmpOli.isValid());
    const QString mtlOli = writeFakeLandsatScene(QDir(tmpOli.path()));
    SatelliteProducts::ProductInfo oliInfo;
    REQUIRE(SatelliteProducts::discoverLandsat(mtlOli, &oliInfo));
    for (const auto& b : oliInfo.bands) {
        if (b.name == QStringLiteral("B4"))
            CHECK(b.wavelengthNm == 655); // OLI Red
        if (b.name == QStringLiteral("B5"))
            CHECK(b.wavelengthNm == 865); // OLI NIR
    }
}

// ---------------------------------------------------------------------------
// #680: Level-2 imports stamp SICNU_NUMERIC_SCALE without rescaling pixels
// ---------------------------------------------------------------------------

TEST_CASE("Landsat L2 stack stamps SICNU_NUMERIC_SCALE from uniform REFLECTANCE_MULT (#680)",
          "[satellite][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QStringList extra = {
        QStringLiteral("REFLECTANCE_MULT_BAND_2 = 0.0000275"),
        QStringLiteral("REFLECTANCE_MULT_BAND_3 = 0.0000275"),
        QStringLiteral("REFLECTANCE_MULT_BAND_4 = 0.0000275"),
        QStringLiteral("REFLECTANCE_MULT_BAND_5 = 0.0000275"),
    };
    const QString mtl = writeFakeLandsatScene(QDir(tmp.path()),
                                              QStringLiteral("LANDSAT_8"),
                                              QStringLiteral("L2SP"), extra);
    const QString out = tmp.filePath(QStringLiteral("landsat_l2_stack.tif"));

    SatelliteProducts::ProductInfo info;
    REQUIRE(SatelliteProducts::discoverLandsat(mtl, &info));
    QString err;
    REQUIRE(SatelliteProducts::stackToGeoTiff(
        info, {QStringLiteral("B4"), QStringLiteral("B5")}, out, &err));

    GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);
    const char* state = GDALGetMetadataItem(ds, "SICNU_RADIOMETRIC_STATE", nullptr);
    REQUIRE(state != nullptr);
    CHECK(QString::fromUtf8(state) == QStringLiteral("surface_reflectance"));
    const char* scale = GDALGetMetadataItem(ds, "SICNU_NUMERIC_SCALE", nullptr);
    REQUIRE(scale != nullptr);
    // REFLECTANCE_MULT is already reflectance-per-DN: the stamped divisor is
    // its reciprocal (1 / 0.0000275 = 36363.636...).
    CHECK(QString::fromUtf8(scale) == QString::number(1.0 / 0.0000275, 'g', 12));

    // Pixels stay verbatim (back-compat): no implicit rescaling at import.
    GDALRasterBandH red = GDALGetRasterBand(ds, 1);
    REQUIRE(red != nullptr);
    std::vector<float> pixels(16);
    REQUIRE(GDALRasterIO(red, GF_Read, 0, 0, 4, 4, pixels.data(), 4, 4,
                         GDT_Float32, 0, 0) == CE_None);
    for (float v : pixels)
        REQUIRE_THAT(v, Catch::Matchers::WithinAbs(80.0f, 0.0f)); // B4 fill value
    GDALClose(ds);
}

TEST_CASE("Landsat L1 and non-uniform-mult stacks carry no numeric scale (#680)",
          "[satellite][landsat]")
{
    ensureApp();

    SECTION("Level-1 (DN) product") {
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QString mtl = writeFakeLandsatScene(QDir(tmp.path()));
        const QString out = tmp.filePath(QStringLiteral("l1_stack.tif"));
        SatelliteProducts::ProductInfo info;
        REQUIRE(SatelliteProducts::discoverLandsat(mtl, &info));
        QString err;
        REQUIRE(SatelliteProducts::stackToGeoTiff(
            info, {QStringLiteral("B4"), QStringLiteral("B5")}, out, &err));
        GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        CHECK(GDALGetMetadataItem(ds, "SICNU_NUMERIC_SCALE", nullptr) == nullptr);
        GDALClose(ds);
    }

    SECTION("Level-2 with non-uniform REFLECTANCE_MULT: stamp nothing") {
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QStringList mixed = {
            QStringLiteral("REFLECTANCE_MULT_BAND_2 = 0.0000275"),
            QStringLiteral("REFLECTANCE_MULT_BAND_4 = 0.0000275"),
            QStringLiteral("REFLECTANCE_MULT_BAND_5 = 0.0000135"),
        };
        const QString mtl = writeFakeLandsatScene(QDir(tmp.path()),
                                                  QStringLiteral("LANDSAT_8"),
                                                  QStringLiteral("L2SP"), mixed);
        const QString out = tmp.filePath(QStringLiteral("l2_mixed_stack.tif"));
        SatelliteProducts::ProductInfo info;
        REQUIRE(SatelliteProducts::discoverLandsat(mtl, &info));
        QString err;
        REQUIRE(SatelliteProducts::stackToGeoTiff(
            info, {QStringLiteral("B2"), QStringLiteral("B4"), QStringLiteral("B5")}, out, &err));
        GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        CHECK(GDALGetMetadataItem(ds, "SICNU_NUMERIC_SCALE", nullptr) == nullptr);
        GDALClose(ds);
    }
}

TEST_CASE("Sentinel-2 L2A stack stamps SICNU_NUMERIC_SCALE (MTD quantification or 10000) (#680)",
          "[satellite][sentinel2]")
{
    ensureApp();

    SECTION("Default when the MTD carries no quantification tag") {
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QString safe = writeFakeSentinel2Safe(QDir(tmp.path()));
        const QString out = tmp.filePath(QStringLiteral("s2_default_scale.tif"));
        SatelliteProducts::ProductInfo info;
        REQUIRE(SatelliteProducts::discoverSentinel2(safe, &info));
        QString err;
        REQUIRE(SatelliteProducts::stackToGeoTiff(
            info, {QStringLiteral("B4"), QStringLiteral("B8")}, out, &err));

        GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        const char* scale = GDALGetMetadataItem(ds, "SICNU_NUMERIC_SCALE", nullptr);
        REQUIRE(scale != nullptr);
        CHECK(QString::fromUtf8(scale) == QStringLiteral("10000"));
        const char* state = GDALGetMetadataItem(ds, "SICNU_RADIOMETRIC_STATE", nullptr);
        REQUIRE(state != nullptr);
        CHECK(QString::fromUtf8(state) == QStringLiteral("surface_reflectance"));
        GDALClose(ds);
    }

    SECTION("BOA_QUANTIFICATION_VALUE from the MTD wins") {
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        // Variant of writeFakeSentinel2Safe with an explicit quantification.
        const QString safe = tmp.filePath(
            QStringLiteral("S2A_MSIL2A_20200615T000000_N9999_R000_T32TQQ_20200615T000000.SAFE"));
        const QString img = safe + QStringLiteral("/GRANULE/L2A_T32TQQ/IMG_DATA/R10m");
        QDir().mkpath(img);
        QFile mtd(QDir(safe).filePath(QStringLiteral("MTD_MSIL2A.xml")));
        REQUIRE(mtd.open(QIODevice::WriteOnly | QIODevice::Text));
        mtd.write(
            "<n1:Level-2A_User_Product>"
            "<General_Info><Product_Image_Characteristics><Radiometric_Info>"
            "<BOA_QUANTIFICATION_VALUE>5000</BOA_QUANTIFICATION_VALUE>"
            "</Radiometric_Info></Product_Image_Characteristics></General_Info>"
            "</n1:Level-2A_User_Product>");
        mtd.close();
        writeTinyBand(img + QStringLiteral("/T32TQQ_20200615T000000_B04_10m.tif"), 40.f);
        writeTinyBand(img + QStringLiteral("/T32TQQ_20200615T000000_B08_10m.tif"), 180.f);

        const QString out = tmp.filePath(QStringLiteral("s2_quant_scale.tif"));
        SatelliteProducts::ProductInfo info;
        QString err;
        REQUIRE(SatelliteProducts::discoverSentinel2(safe, &info, QStringLiteral("10m"), &err));
        REQUIRE(SatelliteProducts::stackToGeoTiff(
            info, {QStringLiteral("B4"), QStringLiteral("B8")}, out, &err));
        GDALDatasetH ds = GDALOpen(out.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        const char* scale = GDALGetMetadataItem(ds, "SICNU_NUMERIC_SCALE", nullptr);
        REQUIRE(scale != nullptr);
        CHECK(QString::fromUtf8(scale) == QStringLiteral("5000"));
        GDALClose(ds);
    }
}

// ---------------------------------------------------------------------------
// #676: import fails closed listing unresolvable bands
// ---------------------------------------------------------------------------

TEST_CASE("Landsat import fails closed listing missingBands (#676)", "[satellite][landsat][operators]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString mtl = writeFakeLandsatScene(QDir(tmp.path()));

    SECTION("Kernel stackToGeoTiff refuses partial band lists") {
        SatelliteProducts::ProductInfo info;
        REQUIRE(SatelliteProducts::discoverLandsat(mtl, &info));
        const QString out = tmp.filePath(QStringLiteral("partial.tif"));
        QString err;
        REQUIRE_FALSE(SatelliteProducts::stackToGeoTiff(
            info, {QStringLiteral("B4"), QStringLiteral("B9")}, out, &err));
        REQUIRE(err.contains(QStringLiteral("missingBands")));
        REQUIRE(err.contains(QStringLiteral("B9")));
        REQUIRE_FALSE(QFileInfo::exists(out));
    }

    SECTION("rs:landsat_import throws RSOperatorError naming the missing bands") {
        auto op = RSOperatorRegistry::instance().create("rs:landsat_import");
        REQUIRE(op != nullptr);
        Json::Value params(Json::objectValue);
        params["input"] = mtl.toStdString();
        params["output"] = (tmp.path() + QStringLiteral("/op_partial.tif")).toStdString();
        params["bands"] = Json::Value(Json::arrayValue);
        params["bands"].append("B2");
        params["bands"].append("B7");
        params["bands"].append("B9");
        RSOperatorContext ctx;
        try {
            (void)op->execute(params, ctx);
            FAIL("rs:landsat_import accepted a partially unresolvable band list");
        } catch (const RSOperatorError& e) {
            const std::string msg = e.message();
            REQUIRE(msg.find("missingBands") != std::string::npos);
            REQUIRE(msg.find("B7") != std::string::npos);
            REQUIRE(msg.find("B9") != std::string::npos);
        }
        REQUIRE_FALSE(QFile::exists(tmp.path() + QStringLiteral("/op_partial.tif")));
    }
}

TEST_CASE("Sentinel-2 import fails closed listing missingBands (#676)", "[satellite][sentinel2][operators]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString safe = writeFakeSentinel2Safe(QDir(tmp.path()));

    auto op = RSOperatorRegistry::instance().create("rs:sentinel2_import");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = safe.toStdString();
    params["output"] = (tmp.path() + QStringLiteral("/s2_partial.tif")).toStdString();
    params["resolution"] = "10m";
    params["bands"] = Json::Value(Json::arrayValue);
    params["bands"].append("B4");
    params["bands"].append("B12"); // 20 m band, not discovered at 10 m
    RSOperatorContext ctx;
    REQUIRE_THROWS_AS(op->execute(params, ctx), RSOperatorError);
    REQUIRE_FALSE(QFile::exists(tmp.path() + QStringLiteral("/s2_partial.tif")));
}

// ---------------------------------------------------------------------------
// #680: rs:spectral_index divides by SICNU_NUMERIC_SCALE for EVI/SAVI
// ---------------------------------------------------------------------------

TEST_CASE("rs:spectral_index EVI/SAVI honour SICNU_NUMERIC_SCALE (#680)",
          "[operators][spectral][landsat]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // 4-band DN-scale stack (Blue, Green, Red, NIR) stamped scale=10000.
    // Reflectance: blue=0.1, red=0.2, nir=0.4.
    const QString input = tmp.path() + QStringLiteral("/dn_stack.tif");
    {
        ensureGdalInit();
        constexpr int W = 4, H = 4;
        const std::vector<float> fills = {1000.f, 1500.f, 2000.f, 4000.f};
        QString err;
        std::vector<std::vector<float>> bands;
        for (float f : fills)
            bands.emplace_back(static_cast<size_t>(W * H), f);
        std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
        REQUIRE(writeGdalOutput(input, W, H, bands, gt,
                                QStringLiteral("EPSG:32648"), &err));
        GDALDatasetH ds = GDALOpen(input.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        const char* roles[] = {"blue", "green", "red", "nir"};
        for (int b = 1; b <= 4; ++b) {
            GDALSetMetadataItem(GDALGetRasterBand(ds, b), "SICNU_BAND_ROLE", roles[b - 1], nullptr);
        }
        GDALSetMetadataItem(ds, "SICNU_NUMERIC_SCALE", "10000", nullptr);
        GDALSetMetadataItem(ds, "SICNU_RADIOMETRIC_STATE", "surface_reflectance", nullptr);
        GDALClose(ds);
    }

    auto op = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;

    // Hand-computed expectations in unit reflectance space:
    //   EVI  = 2.5*(0.4-0.2) / (0.4 + 6*0.2 - 7.5*0.1 + 1.0) = 0.5 / 1.85
    //   SAVI = (0.4-0.2) / (0.4+0.2+0.5) * 1.5               = 0.3/1.1
    // Without the scale guard, EVI on raw DN would be ~0.588 (wrong).
    const double expectedEvi = 2.5 * 0.2 / 1.85;
    const double expectedSavi = 0.2 / 1.1 * 1.5;
    const double unscaledEvi = 2.5 * 2000.0 / (4000.0 + 6 * 2000.0 - 7.5 * 1000.0 + 1.0);

    auto runIndex = [&](const char* index) {
        const QString output = tmp.path() + QStringLiteral("/")
                               + QString::fromLatin1(index).toLower() + QStringLiteral(".tif");
        Json::Value params(Json::objectValue);
        params["input"] = input.toStdString();
        params["output"] = output.toStdString();
        params["index"] = index;
        op->execute(params, ctx);
        return output;
    };

    {
        const QString out = runIndex("EVI");
        GdalDatasetWrapper ds;
        REQUIRE(ds.open(out));
        std::vector<float> pixels(16);
        REQUIRE(ds.readBandData(1, pixels.data(), 4, 4));
        for (float v : pixels)
            REQUIRE_THAT(v, Catch::Matchers::WithinAbs(expectedEvi, 1e-4));
    }
    {
        const QString out = runIndex("SAVI");
        GdalDatasetWrapper ds;
        REQUIRE(ds.open(out));
        std::vector<float> pixels(16);
        REQUIRE(ds.readBandData(1, pixels.data(), 4, 4));
        for (float v : pixels)
            REQUIRE_THAT(v, Catch::Matchers::WithinAbs(expectedSavi, 1e-4));
    }
    // Ratio indices are scale-invariant: NDVI reads DN verbatim and stays exact.
    {
        const QString out = runIndex("NDVI");
        GdalDatasetWrapper ds;
        REQUIRE(ds.open(out));
        std::vector<float> pixels(16);
        REQUIRE(ds.readBandData(1, pixels.data(), 4, 4));
        for (float v : pixels)
            REQUIRE_THAT(v, Catch::Matchers::WithinAbs(0.2f / 0.6f, 1e-5));
    }
    // Sanity: the guard actually changed something (unscaled EVI differs).
    REQUIRE(std::abs(unscaledEvi - expectedEvi) > 0.1);
}

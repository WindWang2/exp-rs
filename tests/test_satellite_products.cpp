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
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_landsat_import_operator.h"
#include "operators/rs/rs_sentinel2_import_operator.h"
#include "operators/rs/rs_modis_import_operator.h"
#include "operators/rs/rs_modis_georeference_operator.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <array>
#include <cmath>
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

QString writeFakeLandsatScene(const QDir& root)
{
    const QString scene = root.filePath(QStringLiteral("LC08_L1TP_TEST"));
    QDir().mkpath(scene);
    QDir sdir(scene);

    // Minimal MTL
    QFile mtl(sdir.filePath(QStringLiteral("LC08_L1TP_TEST_MTL.txt")));
    REQUIRE(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&mtl);
    out << "GROUP = LANDSAT_METADATA_FILE\n";
    out << "  SPACECRAFT_ID = \"LANDSAT_8\"\n";
    out << "  PROCESSING_LEVEL = \"L1TP\"\n";
    out << "  DATE_ACQUIRED = \"2020-06-15\"\n";
    out << "  LANDSAT_PRODUCT_ID = \"LC08_L1TP_TEST\"\n";
    out << "  FILE_NAME_BAND_2 = \"LC08_L1TP_TEST_B2.TIF\"\n";
    out << "  FILE_NAME_BAND_3 = \"LC08_L1TP_CLASS_B3.TIF\"\n";
    out << "  FILE_NAME_BAND_4 = \"LC08_L1TP_CLASS_B4.TIF\"\n";
    out << "  FILE_NAME_BAND_5 = \"LC08_L1TP_CLASS_B5.TIF\"\n";
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

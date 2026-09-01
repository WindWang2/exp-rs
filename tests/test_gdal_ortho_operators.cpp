/***************************************************************************
 * test_gdal_ortho_operators.cpp  —  Tests for GDAL-based RSOperators
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/gdal/gdal_orthorectification_operator.h"
#include "operators/gdal/gdal_reproject_operator.h"
#include "operators/gdal/gdal_clip_operator.h"
#include "operators/gdal/gdal_polygonize_operator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>
#include <cpl_string.h>
#include <ogr_srs_api.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>

using namespace sicnu::operators;
using namespace sicnu::operators::gdal;

namespace {

void initGCP(GDAL_GCP* gcp, double pixel, double line, double x, double y, double z,
             const QString& /*projection*/) {
    gcp->pszId = nullptr;
    gcp->pszInfo = nullptr;
    gcp->dfGCPPixel = pixel;
    gcp->dfGCPLine = line;
    gcp->dfGCPX = x;
    gcp->dfGCPY = y;
    gcp->dfGCPZ = z;
}

QString createTestRaster(const QString& dir, const QString& name, int w, int h) {
    ensureGdalInit();

    QString path = dir + QDir::separator() + name;

    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), w, h, 1, GDT_Byte, opts);
    CSLDestroy(opts);
    REQUIRE(ds != nullptr);

    std::vector<uint8_t> data(w * h, 128);
    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Byte, 0, 0);
    REQUIRE(err == CE_None);

    GDALClose(ds);
    return path;
}

/** Georeferenced WGS84 raster suitable for reproject/clip tests. */
QString createGeoreferencedRaster(const QString& dir, const QString& name, int w, int h) {
    QString path = createTestRaster(dir, name, w, h);

    GDALDatasetH ds = GDALOpen(path.toUtf8().constData(), GA_Update);
    REQUIRE(ds != nullptr);

    // 0.01° pixel size starting at (10, 20)
    double gt[6] = {10.0, 0.01, 0.0, 20.0 + h * 0.01, 0.0, -0.01};
    REQUIRE(GDALSetGeoTransform(ds, gt) == CE_None);

    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    REQUIRE(OSRImportFromEPSG(srs, 4326) == OGRERR_NONE);
    char* wkt = nullptr;
    REQUIRE(OSRExportToWkt(srs, &wkt) == OGRERR_NONE);
    REQUIRE(GDALSetProjection(ds, wkt) == CE_None);
    CPLFree(wkt);
    OSRDestroySpatialReference(srs);

    GDALClose(ds);
    return path;
}

QString createGcpRaster(const QString& dir, const QString& name, int w, int h) {
    QString path = createTestRaster(dir, name, w, h);

    GDALDatasetH ds = GDALOpen(path.toUtf8().constData(), GA_Update);
    REQUIRE(ds != nullptr);

    // Four corner GCPs mapping pixels to a small WGS84 rectangle.
    GDAL_GCP gcp[4];
    const QString projection = "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]";

    initGCP(&gcp[0], 0, 0, 0.0, 0.0, 0.0, projection);
    initGCP(&gcp[1], w, 0, w * 0.001, 0.0, 0.0, projection);
    initGCP(&gcp[2], w, h, w * 0.001, h * 0.001, 0.0, projection);
    initGCP(&gcp[3], 0, h, 0.0, h * 0.001, 0.0, projection);

    REQUIRE(GDALSetGCPs(ds, 4, gcp, projection.toUtf8().constData()) == CE_None);

    GDALClose(ds);
    return path;
}

} // anonymous namespace

TEST_CASE("GdalOrthorectificationOperator schema and metadata", "[gdal]") {
    auto op = std::make_unique<GdalOrthorectificationOperator>();
    REQUIRE(op->name() == "gdal:orthorectification");

    Json::Value schema = op->schema();
    REQUIRE(schema["properties"].isMember("input"));
    REQUIRE(schema["properties"].isMember("output"));
    REQUIRE(schema["properties"].isMember("dem"));
    REQUIRE(schema["properties"].isMember("dstCrs"));
    REQUIRE(schema["properties"].isMember("resampling"));
    REQUIRE(schema["properties"]["resampling"]["default"].asString() == "bilinear");

    Json::Value meta = op->metadata();
    REQUIRE(meta["group"].asString() == "gdal-geometry");
}

TEST_CASE("GdalOrthorectificationOperator rejects raster without RPC/GCP", "[gdal]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "plain.tif", 16, 16);
    QString output = tempDir.path() + QDir::separator() + "out.tif";

    auto op = std::make_unique<GdalOrthorectificationOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::InvalidInputData);
    }
}

TEST_CASE("Orthorectification cancel leaves no partial output", "[gdal][cancel]") {
    // #694: a cancelled warp must not leave a partial GTiff at the user's
    // output path. The ortho operator used to inline the warp without the
    // cancel check, so a cancelled run kept the truncated file.
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createGcpRaster(tempDir.path(), "cancel_in.tif", 32, 32);
    QString output = tempDir.path() + QDir::separator() + "cancel_out.tif";

    auto op = std::make_unique<GdalOrthorectificationOperator>();
    std::atomic<bool> cancelled{true};
    RSOperatorContext ctx;
    ctx.setCancelFlag(&cancelled);

    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();

    bool cancelledThrown = false;
    try {
        (void)op->run(params, ctx);
        FAIL("Expected RSOperatorError(Cancelled)");
    } catch (const RSOperatorError& e) {
        cancelledThrown = (e.code() == ErrorCode::Cancelled);
    }
    REQUIRE(cancelledThrown);
    REQUIRE_FALSE(QFile::exists(output));
}

TEST_CASE("GdalOrthorectificationOperator orthorectifies GCP raster", "[gdal]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createGcpRaster(tempDir.path(), "gcp_in.tif", 32, 32);
    QString output = tempDir.path() + QDir::separator() + "gcp_out.tif";

    auto op = std::make_unique<GdalOrthorectificationOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["dstCrs"] = "EPSG:4326";
    params["targetResolution"] = 0.001;

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    REQUIRE(result["width"].asInt() > 0);
    REQUIRE(result["height"].asInt() > 0);

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(output));
    REQUIRE(!ds.projection().isEmpty());
    REQUIRE(ds.width() > 0);
    REQUIRE(ds.height() > 0);
}

TEST_CASE("GdalOrthorectificationOperator rejects invalid resampling", "[gdal]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    auto op = std::make_unique<GdalOrthorectificationOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = "/nonexistent/input.tif";
    params["output"] = tempDir.filePath("out.tif").toStdString();
    params["resampling"] = "unsupported";

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::InvalidEnumValue);
    }
}

TEST_CASE("GDAL operators are registered", "[gdal]") {
    RSOperatorRegistry& reg = RSOperatorRegistry::instance();
    REQUIRE(reg.hasOperator("gdal:orthorectification"));
    REQUIRE(reg.hasOperator("gdal:reproject"));
    REQUIRE(reg.hasOperator("gdal:clip"));
    REQUIRE(reg.hasOperator("gdal:polygonize"));

    auto op = reg.create("gdal:orthorectification");
    REQUIRE(op != nullptr);
    REQUIRE(op->name() == "gdal:orthorectification");

    auto reproject = reg.create("gdal:reproject");
    REQUIRE(reproject != nullptr);
    REQUIRE(reproject->name() == "gdal:reproject");

    auto clip = reg.create("gdal:clip");
    REQUIRE(clip != nullptr);
    REQUIRE(clip->name() == "gdal:clip");
}

TEST_CASE("GdalReprojectOperator schema and metadata", "[gdal]") {
    auto op = std::make_unique<GdalReprojectOperator>();
    REQUIRE(op->name() == "gdal:reproject");

    Json::Value schema = op->schema();
    REQUIRE(schema["properties"].isMember("input"));
    REQUIRE(schema["properties"].isMember("output"));
    REQUIRE(schema["properties"].isMember("dstCrs"));
    REQUIRE(schema["properties"].isMember("reference"));
    REQUIRE(schema["properties"].isMember("resampling"));

    Json::Value meta = op->metadata();
    REQUIRE(meta["group"].asString() == "gdal-geometry");
    REQUIRE(meta["provider"].asString() == "gdal");
}

TEST_CASE("GdalReprojectOperator rejects missing dstCrs", "[gdal]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    auto op = std::make_unique<GdalReprojectOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = tempDir.filePath("in.tif").toStdString();
    params["output"] = tempDir.filePath("out.tif").toStdString();

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::MissingRequiredParameter);
    }
}

TEST_CASE("GdalReprojectOperator reprojects georeferenced raster to EPSG:3857", "[gdal]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createGeoreferencedRaster(tempDir.path(), "wgs84.tif", 32, 32);
    QString output = tempDir.path() + QDir::separator() + "webmerc.tif";

    auto op = std::make_unique<GdalReprojectOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["dstCrs"] = "EPSG:3857";
    params["resampling"] = "nearest";

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    REQUIRE(result["width"].asInt() > 0);
    REQUIRE(result["height"].asInt() > 0);
    REQUIRE(result["dstCrs"].asString() == "EPSG:3857");

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(output));
    REQUIRE(ds.width() > 0);
    REQUIRE(ds.height() > 0);
    REQUIRE(!ds.projection().isEmpty());
}

TEST_CASE("GdalReprojectOperator aligns output to a reference raster grid", "[gdal]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createGeoreferencedRaster(tempDir.path(), "wgs84.tif", 32, 32);

    // Reference: 30 m UTM grid, 4x4, origin (500000, 4500000).
    QString reference = tempDir.path() + QDir::separator() + "ref.tif";
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);
    GDALDatasetH refDs = GDALCreate(driver, reference.toUtf8().constData(), 4, 4, 1, GDT_Byte, nullptr);
    REQUIRE(refDs != nullptr);
    double refGt[6] = {500000.0, 30.0, 0.0, 4500000.0, 0.0, -30.0};
    REQUIRE(GDALSetGeoTransform(refDs, refGt) == CE_None);
    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    REQUIRE(OSRImportFromEPSG(srs, 32650) == OGRERR_NONE);
    char* wkt = nullptr;
    REQUIRE(OSRExportToWkt(srs, &wkt) == OGRERR_NONE);
    REQUIRE(GDALSetProjection(refDs, wkt) == CE_None);
    CPLFree(wkt);
    OSRDestroySpatialReference(srs);
    GDALClose(refDs);

    QString output = tempDir.path() + QDir::separator() + "aligned.tif";

    auto op = std::make_unique<GdalReprojectOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["reference"] = reference.toStdString();
    params["output"] = output.toStdString();
    params["resampling"] = "nearest";

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    CHECK(result["aligned"].asBool() == true);
    CHECK(result["width"].asInt() == 4);
    CHECK(result["height"].asInt() == 4);

    // The output lands exactly on the reference grid: same dimensions,
    // pixel size, origin, and CRS.
    GdalDatasetWrapper out;
    REQUIRE(out.open(output));
    CHECK(out.width() == 4);
    CHECK(out.height() == 4);
    const auto outGt = out.geoTransform();
    for (int i = 0; i < 6; ++i)
        CHECK(outGt[i] == Catch::Approx(refGt[i]).margin(1e-6));
    CHECK(out.projection().contains(QStringLiteral("32650")));
}

TEST_CASE("GdalClipOperator schema and rejects missing clip source", "[gdal]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    auto op = std::make_unique<GdalClipOperator>();
    REQUIRE(op->name() == "gdal:clip");

    Json::Value schema = op->schema();
    REQUIRE(schema["properties"].isMember("cutline"));
    REQUIRE(schema["properties"].isMember("extent"));

    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = tempDir.filePath("in.tif").toStdString();
    params["output"] = tempDir.filePath("out.tif").toStdString();

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::MissingRequiredParameter);
    }
}

TEST_CASE("GdalClipOperator clips by extent", "[gdal]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    // Raster covers lon [10, 10.32], lat [20, 20.32] at 0.01°/px, 32x32
    QString input = createGeoreferencedRaster(tempDir.path(), "extent_in.tif", 32, 32);
    QString output = tempDir.path() + QDir::separator() + "extent_out.tif";

    auto op = std::make_unique<GdalClipOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["extent"] = Json::Value(Json::arrayValue);
    params["extent"].append(10.05);
    params["extent"].append(20.05);
    params["extent"].append(10.15);
    params["extent"].append(20.15);
    params["resampling"] = "nearest";

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    REQUIRE(result["width"].asInt() > 0);
    REQUIRE(result["height"].asInt() > 0);

    // Clipped extent is smaller than full 32x32
    REQUIRE(result["width"].asInt() < 32);
    REQUIRE(result["height"].asInt() < 32);

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(output));
    REQUIRE(ds.width() == result["width"].asInt());
    REQUIRE(ds.height() == result["height"].asInt());
}


TEST_CASE("GdalPolygonizeOperator schema", "[gdal]") {
    auto op = std::make_unique<GdalPolygonizeOperator>();
    REQUIRE(op->name() == "gdal:polygonize");
    Json::Value schema = op->schema();
    REQUIRE(schema["properties"].isMember("field"));
    REQUIRE(schema["properties"].isMember("connected8"));
}

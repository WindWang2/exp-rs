/***************************************************************************
 * test_otb_operators.cpp  —  Tests for OTB CLI-based RSOperators
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/otb/otb_compute_images_statistics_operator.h"
#include "operators/otb/otb_segmentation_operator.h"
#include "operators/otb/otb_svm_classification_operator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/tools/tool_path_manager.h"

#include <gdal.h>
#include <cpl_string.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>
#include <ogrsf_frmts.h>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <array>
#include <vector>

using namespace sicnu::operators;
using namespace sicnu::operators::otb;

namespace {

bool otbCliAvailable(const QString& appName) {
    return !ToolPathManager::instance().otbToolPath(appName).isEmpty();
}

/**
 * Some composite OTB apps (e.g. TrainImagesClassifier) segfault during Init when
 * ApplicationEngine is linked statically into each otbapp_*.so. Probe -help so
 * execution tests skip instead of failing hard.
 */
bool otbCliHelpWorks(const QString& appName) {
    const QString program = ToolPathManager::instance().otbToolPath(appName);
    if (program.isEmpty())
        return false;
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_NUMERIC"), QStringLiteral("C"));
    const QString bundle = ToolPathManager::instance().otbBundleDir();
    if (!bundle.isEmpty()) {
        const QString appPath = QDir(bundle).filePath(QStringLiteral("lib/otb/applications"));
        if (QFileInfo::exists(appPath))
            env.insert(QStringLiteral("OTB_APPLICATION_PATH"), appPath);
        const QString libPath = QDir(bundle).filePath(QStringLiteral("lib"));
        const QString ld = env.value(QStringLiteral("LD_LIBRARY_PATH"));
        env.insert(QStringLiteral("LD_LIBRARY_PATH"),
                   libPath + (ld.isEmpty() ? QString() : QStringLiteral(":") + ld));
    }
    proc.setProcessEnvironment(env);
    proc.start(program, {QStringLiteral("-help")});
    if (!proc.waitForFinished(15000)) {
        proc.kill();
        return false;
    }
    // Exit 1 is normal for OTB -help (prints usage); crash / signal → fail.
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() < 128;
}

QString createTestRaster(const QString& dir, const QString& name, int w, int h, int bands = 1) {
    ensureGdalInit();

    QString path = dir + QDir::separator() + name;

    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), w, h, bands, GDT_Float32, opts);
    CSLDestroy(opts);
    REQUIRE(ds != nullptr);

    std::vector<float> data(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            data[y * w + x] = static_cast<float>((x + y) % 256);
        }
    }

    for (int b = 1; b <= bands; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b);
        CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
        REQUIRE(err == CE_None);
    }

    // North-up geotransform covering [0,w] x [0,h] so training polygons at
    // positive map coordinates (see createTestVector) fall inside the raster.
    std::array<double, 6> gt = {0.0, 1.0, 0.0, static_cast<double>(h), 0.0, -1.0};
    GDALSetGeoTransform(ds, gt.data());
    GDALSetProjection(ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");

    GDALClose(ds);
    return path;
}

QString createTestVector(const QString& dir, const QString& name) {
    ensureGdalInit();

    QString path = dir + QDir::separator() + name;

    GDALDriverH driver = OGRGetDriverByName("ESRI Shapefile");
    REQUIRE(driver != nullptr);

    GDALDatasetH ds = OGR_Dr_CreateDataSource(driver, path.toUtf8().constData(), nullptr);
    REQUIRE(ds != nullptr);

    // Same WGS84 as createTestRaster so OTB sampling can reproject/match extents.
    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    OSRSetFromUserInput(srs, "EPSG:4326");
    OGRLayerH layer = GDALDatasetCreateLayer(ds, "training", srs, wkbPolygon, nullptr);
    OSRDestroySpatialReference(srs);
    REQUIRE(layer != nullptr);

    OGRFieldDefnH field = OGR_Fld_Create("Class", OFTInteger);
    REQUIRE(OGR_L_CreateField(layer, field, TRUE) == OGRERR_NONE);
    OGR_Fld_Destroy(field);

    auto createPolygon = [](double xmin, double ymin, double xmax, double ymax) {
        OGRGeometryH ring = OGR_G_CreateGeometry(wkbLinearRing);
        OGR_G_AddPoint(ring, xmin, ymin, 0);
        OGR_G_AddPoint(ring, xmax, ymin, 0);
        OGR_G_AddPoint(ring, xmax, ymax, 0);
        OGR_G_AddPoint(ring, xmin, ymax, 0);
        OGR_G_AddPoint(ring, xmin, ymin, 0);
        OGRGeometryH poly = OGR_G_CreateGeometry(wkbPolygon);
        OGR_G_AddGeometryDirectly(poly, ring);
        return poly;
    };

    struct Sample { double xmin, ymin, xmax, ymax; int label; };
    std::vector<Sample> samples = {
        {1.0, 1.0, 3.0, 3.0, 1},
        {5.0, 1.0, 7.0, 3.0, 2},
        {1.0, 5.0, 3.0, 7.0, 1},
        {5.0, 5.0, 7.0, 7.0, 2},
    };

    for (const auto& s : samples) {
        OGRFeatureH feat = OGR_F_Create(OGR_L_GetLayerDefn(layer));
        OGRGeometryH geom = createPolygon(s.xmin, s.ymin, s.xmax, s.ymax);
        OGR_F_SetGeometry(feat, geom);
        OGR_G_DestroyGeometry(geom);
        OGR_F_SetFieldInteger(feat, OGR_F_GetFieldIndex(feat, "Class"), s.label);
        REQUIRE(OGR_L_CreateFeature(layer, feat) == OGRERR_NONE);
        OGR_F_Destroy(feat);
    }

    GDALClose(ds);
    return path;
}

} // anonymous namespace

TEST_CASE("OtbSegmentationOperator schema and metadata", "[otb]") {
    auto op = std::make_unique<OtbSegmentationOperator>();
    REQUIRE(op->name() == "otb:meanshift_segmentation");

    Json::Value schema = op->schema();
    REQUIRE(schema["properties"].isMember("input"));
    REQUIRE(schema["properties"].isMember("output"));
    REQUIRE(schema["properties"].isMember("filter"));
    REQUIRE(schema["properties"]["filter"]["default"].asString() == "meanshift");
    REQUIRE(schema["properties"].isMember("outputMode"));
    REQUIRE(schema["properties"].isMember("spatialRadius"));

    Json::Value meta = op->metadata();
    REQUIRE(meta["group"].asString() == "otb-segmentation");
}

TEST_CASE("OtbSegmentationOperator rejects invalid filter", "[otb]") {
    auto op = std::make_unique<OtbSegmentationOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = "/nonexistent/input.tif";
    params["output"] = "/tmp/out.shp";
    params["filter"] = "invalid_filter";

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::InvalidEnumValue);
    }
}

TEST_CASE("OtbSegmentationOperator execution", "[otb]") {
    if (!otbCliAvailable("Segmentation")) {
        SKIP("OTB Segmentation CLI not available");
    }

    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "seg_in.tif", 64, 64, 3);
    QString output = tempDir.path() + QDir::separator() + "seg_out.shp";

    auto op = std::make_unique<OtbSegmentationOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["filter"] = "meanshift";
    params["outputMode"] = "vector";
    params["spatialRadius"] = 3;
    params["rangeRadius"] = 10.0;
    params["minRegionSize"] = 1;
    params["maxIterations"] = 50;

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
    REQUIRE(QFile::exists(output));
}

TEST_CASE("OtbSvmClassificationOperator schema and metadata", "[otb]") {
    auto op = std::make_unique<OtbSvmClassificationOperator>();
    REQUIRE(op->name() == "otb:svm_classification");

    Json::Value schema = op->schema();
    REQUIRE(schema["properties"].isMember("input"));
    REQUIRE(schema["properties"].isMember("vector"));
    REQUIRE(schema["properties"].isMember("labelField"));
    REQUIRE(schema["properties"].isMember("kernel"));
    REQUIRE(schema["properties"]["kernel"]["default"].asString() == "linear");

    Json::Value meta = op->metadata();
    REQUIRE(meta["group"].asString() == "otb-classification");
}

TEST_CASE("OtbSvmClassificationOperator rejects invalid kernel", "[otb]") {
    auto op = std::make_unique<OtbSvmClassificationOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = "/nonexistent/input.tif";
    params["vector"] = "/nonexistent/vector.shp";
    params["output"] = "/tmp/model.txt";
    params["kernel"] = "unknown";

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::InvalidEnumValue);
    }
}

TEST_CASE("OtbSvmClassificationOperator execution", "[otb]") {
    if (!otbCliAvailable("TrainImagesClassifier")) {
        SKIP("OTB TrainImagesClassifier CLI not available");
    }
    if (!otbCliHelpWorks("TrainImagesClassifier")) {
        SKIP("TrainImagesClassifier crashes on load (static ApplicationEngine / composite apps)");
    }

    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "svm_in.tif", 16, 16, 3);
    QString vector = createTestVector(tempDir.path(), "svm_train.shp");
    QString output = tempDir.path() + QDir::separator() + "svm_model.txt";

    auto op = std::make_unique<OtbSvmClassificationOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["vector"] = vector.toStdString();
    params["output"] = output.toStdString();
    params["labelField"] = "Class";
    params["kernel"] = "linear";
    params["C"] = 1.0;

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
}

TEST_CASE("OtbComputeImagesStatisticsOperator schema and metadata", "[otb]") {
    auto op = std::make_unique<OtbComputeImagesStatisticsOperator>();
    REQUIRE(op->name() == "otb:compute_images_statistics");

    Json::Value schema = op->schema();
    REQUIRE(schema["properties"].isMember("input"));
    REQUIRE(schema["properties"].isMember("inputs"));
    REQUIRE(schema["properties"].isMember("output"));

    Json::Value meta = op->metadata();
    REQUIRE(meta["group"].asString() == "otb-classification");
}

TEST_CASE("OtbComputeImagesStatisticsOperator rejects missing inputs", "[otb]") {
    auto op = std::make_unique<OtbComputeImagesStatisticsOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["output"] = "/tmp/stats.xml";

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::MissingRequiredParameter);
    }
}

TEST_CASE("OtbComputeImagesStatisticsOperator execution", "[otb]") {
    if (!otbCliAvailable("ComputeImagesStatistics")) {
        SKIP("OTB ComputeImagesStatistics CLI not available");
    }

    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    QString input = createTestRaster(tempDir.path(), "stats_in.tif", 16, 16, 3);
    QString output = tempDir.path() + QDir::separator() + "stats.xml";

    auto op = std::make_unique<OtbComputeImagesStatisticsOperator>();
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();

    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == output.toStdString());
}

TEST_CASE("OTB operators are registered", "[otb]") {
    RSOperatorRegistry& reg = RSOperatorRegistry::instance();
    REQUIRE(reg.hasOperator("otb:meanshift_segmentation"));
    REQUIRE(reg.hasOperator("otb:svm_classification"));
    REQUIRE(reg.hasOperator("otb:compute_images_statistics"));

    auto op = reg.create("otb:meanshift_segmentation");
    REQUIRE(op != nullptr);
    REQUIRE(op->name() == "otb:meanshift_segmentation");
}

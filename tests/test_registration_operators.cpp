// test_registration_operators.cpp — F13: headless operator surface.
// Known-answer checks over GDAL-written synthetic rasters; refusal paths
// must throw and leave no output file. Nothing is derived from the code
// under test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <fstream>
#include <vector>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

using namespace sicnu::operators;

namespace {

std::uint32_t hashGrain(int x, int y)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                      + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return h;
}

float texture(int x, int y)
{
    const double fx = x, fy = y;
    return static_cast<float>(50.0 + 20.0 * std::sin(fx / 9.3) * std::sin(fy / 7.7)
                              + 10.0 * std::sin((fx + fy) / 5.1)
                              + 3.0 * (static_cast<double>(hashGrain(x, y) & 0xFFFF) / 32767.5
                                       - 1.0));
}

void writeTiff(const QString& path, int dim,
               const std::function<float(int, int)>& pixel)
{
    GDALAllRegister();
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    GDALDataset* ds = driver->Create(path.toUtf8().constData(), dim, dim, 1, GDT_Float32,
                                     nullptr);
    REQUIRE(ds != nullptr);
    // Realistic non-unit, y-flipping geotransform (10 m/px UTM-style): the
    // operator must keep the warp consistent under such grids.
    double gt[6] = {450000.0, 10.0, 0.0, 5000000.0, 0.0, -10.0};
    ds->SetGeoTransform(gt);
    ds->SetProjection("EPSG:32633");
    std::vector<float> buffer(static_cast<std::size_t>(dim) * dim, 0.0f);
    for (int y = 0; y < dim; ++y)
        for (int x = 0; x < dim; ++x)
            buffer[static_cast<std::size_t>(y) * dim + x] = pixel(x, y);
    REQUIRE(ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, dim, dim, buffer.data(), dim, dim,
                                           GDT_Float32, 0, 0)
            == CE_None);
    GDALClose(ds);
}

Json::Value runOperator(const std::string& opId, const Json::Value& params)
{
    auto op = RSOperatorRegistry::instance().create(opId);
    REQUIRE(op != nullptr);
    RSOperatorContext context;
    return op->run(params, context);
}

bool readJsonFile(const QString& path, QJsonObject* out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *out = QJsonDocument::fromJson(file.readAll()).object();
    return true;
}

} // namespace

TEST_CASE("rs:register_images recovers a shifted pair and writes output + report",
          "[f13][operator]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const int kDim = 256;
    const double kDx = 4.0, kDy = 6.0;
    const QString srcPath = dir.filePath("src.tif");
    const QString refPath = dir.filePath("ref.tif");
    const QString outPath = dir.filePath("registered.tif");
    const QString reportPath = dir.filePath("report.json");
    writeTiff(srcPath, kDim, [](int x, int y) { return texture(x, y); });
    writeTiff(refPath, kDim,
              [](int x, int y) { return texture(x - 4, y - 6); });

    Json::Value params(Json::objectValue);
    params["source"] = srcPath.toStdString();
    params["reference"] = refPath.toStdString();
    params["output"] = outPath.toStdString();
    params["reportPath"] = reportPath.toStdString();
    params["metric"] = "phase_correlation";
    params["maxDim"] = 256;
    const Json::Value result = runOperator("rs:register_images", params);

    REQUIRE(result["status"].asString() == "success");
    REQUIRE(result["inlierCount"].asInt() >= 8);
    REQUIRE(result["rmsePx"].asDouble() < 2.0);
    REQUIRE(QFile::exists(outPath));

    // Content oracle: the registered output must match the reference content
    // (both derive from the same texture; a pixel-space vs world-space mix-up
    // in the warp shows up here as a gross misalignment).
    {
        GDALAllRegister();
        GDALDataset* out = static_cast<GDALDataset*>(
            GDALOpen(outPath.toUtf8().constData(), GA_ReadOnly));
        REQUIRE(out != nullptr);
        std::vector<float> outBuf(static_cast<size_t>(kDim) * kDim, 0.0f);
        REQUIRE(out->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, kDim, kDim, outBuf.data(), kDim,
                                                kDim, GDT_Float32, 0, 0)
                == CE_None);
        GDALClose(out);
        std::vector<float> refBuf(static_cast<size_t>(kDim) * kDim, 0.0f);
        for (int y = 0; y < kDim; ++y)
            for (int x = 0; x < kDim; ++x)
                refBuf[static_cast<size_t>(y) * kDim + x] = texture(x - 4, y - 6);
        double mad = 0.0, validFrac = 0.0;
        size_t valid = 0;
        double acc = 0.0;
        for (int y = 4; y < kDim - 4; ++y)
            for (int x = 4; x < kDim - 4; ++x) {
                const size_t idx = static_cast<size_t>(y) * kDim + x;
                if (std::isnan(outBuf[idx]) || std::isnan(refBuf[idx]))
                    continue;
                if (outBuf[idx] < -9990.f)
                    continue; // warp NoData sentinel at out-of-source borders
                acc += std::abs(static_cast<double>(outBuf[idx]) - static_cast<double>(refBuf[idx]));
                ++valid;
            }
        validFrac = valid;
        mad = valid > 0 ? acc / static_cast<double>(valid) : 1e9;
        REQUIRE(validFrac > 0.9 * (kDim - 8) * (kDim - 8));
        REQUIRE(mad < 3.0);
    }

    // Output grid must equal the reference grid (dim × dim GeoTIFF).
    GDALAllRegister();
    GDALDataset* out = static_cast<GDALDataset*>(
        GDALOpen(outPath.toUtf8().constData(), GA_ReadOnly));
    REQUIRE(out != nullptr);
    REQUIRE(out->GetRasterXSize() == kDim);
    REQUIRE(out->GetRasterYSize() == kDim);
    GDALClose(out);

    // Sidecar carries the F13 quality schema.
    QJsonObject report;
    REQUIRE(readJsonFile(reportPath, &report));
    REQUIRE(report.value("schema").toString() == QStringLiteral("exp_rs_registration_quality/1"));
}

TEST_CASE("rs:register_images refuses a flat pair and writes no output",
          "[f13][operator][negative]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString srcPath = dir.filePath("flat_src.tif");
    const QString refPath = dir.filePath("flat_ref.tif");
    const QString outPath = dir.filePath("must_not_exist.tif");
    writeTiff(srcPath, 128, [](int, int) { return 10.0f; });
    writeTiff(refPath, 128, [](int, int) { return 20.0f; });

    Json::Value params(Json::objectValue);
    params["source"] = srcPath.toStdString();
    params["reference"] = refPath.toStdString();
    params["output"] = outPath.toStdString();
    REQUIRE_THROWS_AS(runOperator("rs:register_images", params), RSOperatorError);
    REQUIRE_FALSE(QFile::exists(outPath));
}

TEST_CASE("rs:stack_register solves a consistent triangle and writes the sidecar",
          "[f13][operator]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString reportPath = dir.filePath("stack.json");

    Json::Value params(Json::objectValue);
    params["scenes"] = Json::Value(Json::arrayValue);
    params["scenes"].append("A");
    params["scenes"].append("B");
    params["scenes"].append("C");
    Json::Value obs(Json::arrayValue);
    auto edge = [&obs](const char* from, const char* to, double tx, double ty) {
        Json::Value o(Json::objectValue);
        o["fromId"] = from;
        o["toId"] = to;
        o["tx"] = tx;
        o["ty"] = ty;
        obs.append(o);
    };
    edge("A", "B", 10.0, 0.0);
    edge("A", "C", 0.0, 5.0);
    edge("B", "C", -10.0, 5.0);
    params["observations"] = obs;
    params["reportPath"] = reportPath.toStdString();

    const Json::Value result = runOperator("rs:stack_register", params);
    REQUIRE(result["status"].asString() == "success");
    REQUIRE(result["reference"].asString() == "A");
    REQUIRE(result["maxEdgeResidualPx"].asDouble() < 1e-6);
    REQUIRE(result["scenes"].size() == 3);

    QJsonObject report;
    REQUIRE(readJsonFile(reportPath, &report));
    REQUIRE(report.value("schema").toString() == QStringLiteral("exp_rs_stack_registration/1"));
}

TEST_CASE("rs:stack_register refuses with no observations", "[f13][operator][negative]")
{
    Json::Value params(Json::objectValue);
    params["scenes"] = Json::Value(Json::arrayValue);
    params["scenes"].append("A");
    params["scenes"].append("B");
    params["observations"] = Json::Value(Json::arrayValue);
    REQUIRE_THROWS_AS(runOperator("rs:stack_register", params), RSOperatorError);
}

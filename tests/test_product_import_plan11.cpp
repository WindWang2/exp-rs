// test_product_import_plan11.cpp — ADR 0159 import-plan hardening: dry-run
// constituent graph with budget-capped sha256 checksums, cooperative
// cancellation with a zero-half-product contract, read-only sources, Chinese
// paths, and the GF-3 SAR family through the full plan→execute chain.
//
// Oracles: digests are cross-checked against the one-shot Sha256 helper over
// independently read file bytes; expectations are literals derived from the
// hand-written fixtures. No real imagery is committed anywhere.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

#include "geospatial/products/cn_product_metadata.h"
#include "geospatial/products/product_adapters.h"
#include "geospatial/util/sha256.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/rs/rs_product_import_plan.h"

#include <array>
#include <atomic>
#include <string>
#include <vector>

char appArgv0[] = "test_product_import_plan11";
int appArgc = 1;
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc, appArgv);
}

void ensureGdalInit()
{
    static bool ready = false;
    if (!ready) {
        GDALAllRegister();
        ready = true;
    }
}

void writeText(const QString &path, const QString &content)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content.toUtf8());
    file.close();
}

void writeStackTiff(const QString &path, int bands, int size = 4)
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);
    const std::array<double, 6> gt = {500000, 8.0, 0, 4400000, 0, -8.0};
    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), size, size, bands,
                                 GDT_Float32, nullptr);
    REQUIRE(ds != nullptr);
    GDALSetGeoTransform(ds, gt.data());
    std::vector<float> line(static_cast<size_t>(size), 1.0f);
    for (int b = 0; b < bands; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
        for (int row = 0; row < size; ++row)
            REQUIRE(GDALRasterIO(band, GF_Write, 0, row, size, 1, line.data(), size, 1,
                                 GDT_Float32, 0, 0) == CE_None);
    }
    GDALClose(ds);
}

/// Independent digest oracle: one-shot Sha256 over bytes read through a
/// separate QFile handle (the dry run hashes by streaming blocks).
QString digestOf(const QString &path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray all = file.readAll();
    file.close();
    return QString::fromStdString(
        sicnu::geo::sha256Hex(std::string(all.constData(), static_cast<size_t>(all.size()))));
}

/// Writes a complete GF-1-style PMS product (sidecar + TIFF + RPC) under
/// @p dir and returns the multispectral image path.
QString writeGf1Product(const QString &dir, bool withRpc = true, int bands = 4)
{
    const QString base = dir + "/GF1_PMS_E113.5_N23.5_20230512_L1A0123456789-MSS1";
    writeStackTiff(base + ".tiff", bands);
    QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                "<MetaInfo>\n"
                                "  <ProductID>10301234567</ProductID>\n"
                                "  <SatelliteID>GF1</SatelliteID>\n"
                                "  <SensorID>PMS1</SensorID>\n"
                                "  <ModeID>PMS</ModeID>\n"
                                "  <ProductLevel>L1A</ProductLevel>\n"
                                "  <ReceiveDate>2023-05-12</ReceiveDate>\n"
                                "  <ReceiveTime>11:03:22</ReceiveTime>\n"
                                "  <OrbitID>09231</OrbitID>\n"
                                "  <PixelSizeX>8.0</PixelSizeX>\n"
                                "  <CloudPercent>12.5</CloudPercent>\n"
                                "  <SunPosGeodetic><Azimuth>151.4</Azimuth>"
                                "<Elevation>62.8</Elevation></SunPosGeodetic>\n");
    for (int i = 1; i <= bands; ++i)
        xml += QStringLiteral("  <BandID>B%1</BandID>\n").arg(i);
    xml += QStringLiteral("  <GainVal>0.0596,0.0575,0.0479,0.0553</GainVal>\n"
                          "  <OffsetVal>-3.4587,-4.6871,-3.6525,-4.9167</OffsetVal>\n"
                          "</MetaInfo>\n");
    writeText(base + ".xml", xml);
    if (withRpc)
        writeText(base + ".rpb",
                  QStringLiteral("satId = \"GF1\";\n"
                                 "lineOff = [0, 1];\n"
                                 "sampOff = [0, 1];\n"));
    return base + ".tiff";
}

TEST_CASE("plan11: dry-run resolves the constituent graph with complete checksums",
          "[plan11][dryrun]")
{
    ensureApp();
    qunsetenv("SICNU_DATA_DIR");
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString image = writeGf1Product(tmp.path());

    const auto dryRun = sicnu::operators::rs::dryRunCnProductImport(image.toStdString(), nullptr);
    REQUIRE(dryRun.checksumAlgorithm == QStringLiteral("sha256"));
    REQUIRE(dryRun.hashBudgetBytes == 268435456);
    REQUIRE(dryRun.plan.identity.kindName == "gaofen_product");
    REQUIRE(dryRun.plan.completeness == sicnu::geo::ProductCompleteness::Complete);

    int seen = 0;
    for (const auto &constituent : dryRun.constituents) {
        INFO("role: " << constituent.role.toStdString());
        REQUIRE(constituent.exists);
        REQUIRE(constituent.readable);
        REQUIRE(constituent.hashComplete);
        REQUIRE(constituent.digestScope == QStringLiteral("file"));
        REQUIRE(constituent.sha256Hex == digestOf(constituent.path).toStdString());
        REQUIRE(constituent.bytes > 0);
        REQUIRE(constituent.hashedBytes == constituent.bytes);
        ++seen;
    }
    // sidecar + image + rpc — every resolved constituent is reported.
    REQUIRE(seen == 3);

    // The stable payload carries the same graph for agents/UI.
    const Json::Value json = dryRun.toJson();
    REQUIRE(json["read_only"].asBool());
    REQUIRE(json["checksum"]["algorithm"].asString() == "sha256");
    REQUIRE(json["constituents"].size() == 3);
    REQUIRE(json["plan"]["productFamily"].asString() == "gaofen_product");
    REQUIRE(json["plan"]["completeness"].asString() == "complete");
}

TEST_CASE("plan11: dry-run reports budget-capped digests explicitly",
          "[plan11][dryrun][budget]")
{
    ensureApp();
    qunsetenv("SICNU_DATA_DIR");
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString image = writeGf1Product(tmp.path());

    const auto dryRun = sicnu::operators::rs::dryRunCnProductImport(image.toStdString(), nullptr,
                                                                    /*hashBudgetBytes=*/64);
    // The cap is per file: files larger than 64 bytes get an explicitly
    // labelled 64-byte prefix digest (never claimed complete); the tiny RPC
    // text file is below the cap and still hashes completely.
    for (const auto &constituent : dryRun.constituents) {
        INFO("role: " << constituent.role.toStdString());
        if (constituent.bytes > 64) {
            REQUIRE_FALSE(constituent.hashComplete);
            REQUIRE(constituent.hashedBytes == 64);
            REQUIRE(constituent.digestScope.contains(QLatin1String("budget cap")));
        } else {
            REQUIRE(constituent.hashComplete);
            REQUIRE(constituent.hashedBytes == constituent.bytes);
            REQUIRE(constituent.digestScope == QStringLiteral("file"));
        }
    }
}

TEST_CASE("plan11: a cancelled dry run is a typed refusal, not a partial report",
          "[plan11][dryrun][cancel]")
{
    ensureApp();
    qunsetenv("SICNU_DATA_DIR");
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString image = writeGf1Product(tmp.path());

    std::atomic<bool> cancelled{true};
    sicnu::operators::RSOperatorContext context;
    context.setCancelFlag(&cancelled);

    bool threw = false;
    try {
        (void)sicnu::operators::rs::dryRunCnProductImport(image.toStdString(), nullptr,
                                                          268435456, &context);
    } catch (const sicnu::operators::RSOperatorError &error) {
        threw = true;
        REQUIRE(error.code() == sicnu::operators::ErrorCode::Cancelled);
    }
    REQUIRE(threw);
}

TEST_CASE("plan11: cancelling mid-stack leaves no half-product on disk",
          "[plan11][cancel][zero-half-product]")
{
    ensureApp();
    qunsetenv("SICNU_DATA_DIR");
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString image = writeGf1Product(tmp.path(), /*withRpc=*/false, /*bands=*/4);
    const QString output = tmp.path() + "/cancelled-stack.tif";

    // Flip the cancel flag once the stack is underway: the bridge lambda in
    // executeCnProductImport calls throwIfCancelled on every progress tick,
    // so the exception unwinds mid-band — exactly the path that used to leak
    // a half-stacked GeoTIFF.
    std::atomic<bool> cancelFlag{false};
    std::atomic<int> progressCalls{0};
    sicnu::operators::RSOperatorContext context;
    context.setCancelFlag(&cancelFlag);
    context.setProgressCallback([&](double, const std::string &) {
        if (++progressCalls >= 2)
            cancelFlag = true;
    });

    const auto plan = sicnu::operators::rs::planCnProductImport(image.toStdString(), nullptr);
    bool threw = false;
    try {
        (void)sicnu::operators::rs::executeCnProductImport(plan, output.toStdString(),
                                                           plan.bandNames,
                                                           /*applyCalibration=*/false, context);
    } catch (const sicnu::operators::RSOperatorError &error) {
        threw = true;
        REQUIRE(error.code() == sicnu::operators::ErrorCode::Cancelled);
    }
    REQUIRE(threw);
    REQUIRE_FALSE(QFile::exists(output));
}

TEST_CASE("plan11: cancelling mid-calibration leaves no half-calibrated product",
          "[plan11][cancel][zero-half-product][calibration]")
{
    ensureApp();
    qunsetenv("SICNU_DATA_DIR");
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // GF-1 fixture with declared GainVal/OffsetVal on every band: calibration
    // is applicable, so the calibration loop actually runs.
    const QString image = writeGf1Product(tmp.path(), /*withRpc=*/false, /*bands=*/4);
    const QString output = tmp.path() + "/calibration-cancelled.tif";

    std::atomic<bool> cancelFlag{false};
    sicnu::operators::RSOperatorContext context;
    context.setCancelFlag(&cancelFlag);
    // Flip the flag once stacking has finished (progress >= 0.9 passes the
    // stack bridge range): the cancel then fires inside the calibration
    // loop's per-line checkpoint — the path that used to strand a file
    // mixing DN and radiance rows.
    context.setProgressCallback([&](double progress, const std::string &) {
        if (progress >= 0.92)
            cancelFlag = true;
    });

    const auto plan = sicnu::operators::rs::planCnProductImport(image.toStdString(), nullptr);
    REQUIRE(sicnu::operators::rs::evaluateCalibration(plan, true, plan.bandNames).applicable);
    bool threw = false;
    try {
        (void)sicnu::operators::rs::executeCnProductImport(plan, output.toStdString(),
                                                           plan.bandNames,
                                                           /*applyCalibration=*/true, context);
    } catch (const sicnu::operators::RSOperatorError &error) {
        threw = true;
        REQUIRE(error.code() == sicnu::operators::ErrorCode::Cancelled);
    }
    REQUIRE(threw);
    REQUIRE_FALSE(QFile::exists(output));
}

TEST_CASE("plan11: read-only sources import cleanly and stay untouched",
          "[plan11][readonly]")
{
    ensureApp();
    qunsetenv("SICNU_DATA_DIR");
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString image = writeGf1Product(tmp.path());

    // Make every source file read-only (honoured on Windows and POSIX).
    const QFileInfo info(image);
    const QDir dir = info.absoluteDir();
    qint64 bytesBefore = 0;
    for (const QString &entry : dir.entryList(QDir::Files)) {
        QFile file(dir.absoluteFilePath(entry));
        REQUIRE(file.open(QIODevice::ReadOnly));
        bytesBefore += file.size();
        file.close();
        const auto perms = QFile::permissions(dir.absoluteFilePath(entry));
        QFile::setPermissions(dir.absoluteFilePath(entry),
                              perms & (QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup |
                                       QFile::ReadOther | QFile::ExeOwner | QFile::ExeUser |
                                       QFile::ExeGroup | QFile::ExeOther));
    }

    // Dry run: read-only over read-only sources; the missing calibration is
    // a declared-field report, not a verdict degradation.
    const auto dryRun =
        sicnu::operators::rs::dryRunCnProductImport(image.toStdString(), nullptr);
    for (const auto &constituent : dryRun.constituents)
        REQUIRE(constituent.readable);

    // Full import: output lands beside the sources but nothing rewrites them.
    const QString output = tmp.filePath("readonly-stack.tif");
    const auto plan = sicnu::operators::rs::planCnProductImport(image.toStdString(), nullptr);
    sicnu::operators::RSOperatorContext context;
    const Json::Value result = sicnu::operators::rs::executeCnProductImport(
        plan, output.toStdString(), {}, /*applyCalibration=*/true, context);
    REQUIRE(QFile::exists(output));
    REQUIRE(result["radiometricState"].asString() == "radiance");

    // Sources unchanged: still read-only, same total bytes (the output sits
    // beside them, so it is excluded from the comparison).
    qint64 bytesAfter = 0;
    for (const QString &entry : dir.entryList(QDir::Files)) {
        if (entry == QFileInfo(output).fileName())
            continue;
        QFile file(dir.absoluteFilePath(entry));
        REQUIRE(file.open(QIODevice::ReadOnly));
        bytesAfter += file.size();
        file.close();
        REQUIRE_FALSE(QFile::permissions(dir.absoluteFilePath(entry)) & QFile::WriteOwner);
    }
    REQUIRE(bytesAfter == bytesBefore);
}

TEST_CASE("plan11: Chinese paths work end to end through the plan service",
          "[plan11][unicode]")
{
    ensureApp();
    qunsetenv("SICNU_DATA_DIR");
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/产品目录/高分一号");
    REQUIRE(QDir().mkpath(dir));
    const QString image = writeGf1Product(dir);

    const auto dryRun = sicnu::operators::rs::dryRunCnProductImport(image.toStdString(), nullptr);
    REQUIRE(dryRun.plan.identity.supported);
    REQUIRE(dryRun.constituents.size() == 3);
    for (const auto &constituent : dryRun.constituents)
        REQUIRE(constituent.readable);

    const QString output = dir + QStringLiteral("/叠加结果.tif");
    const auto plan = sicnu::operators::rs::planCnProductImport(image.toStdString(), nullptr);
    sicnu::operators::RSOperatorContext context;
    const Json::Value result =
        sicnu::operators::rs::executeCnProductImport(plan, output.toStdString(), {}, false, context);
    REQUIRE(QFile::exists(output));
    REQUIRE(result["bandCount"] == 4);
}

TEST_CASE("plan11: GF-3 SAR imports at declared-metadata level with SAR stamps",
          "[plan11][gf3][import]")
{
    ensureApp();
    qunsetenv("SICNU_DATA_DIR");
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString base = tmp.path() + "/GF3_QPS_E113.9_N22.2_20221121_L1A00000012345-HH";
    writeStackTiff(base + ".tiff", 1);
    writeText(base + ".xml",
              QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                             "<MetaInfo>\n"
                             "  <ProductID>31887</ProductID>\n"
                             "  <SatelliteID>GF3</SatelliteID>\n"
                             "  <SensorID>SAR</SensorID>\n"
                             "  <ModeID>QPS</ModeID>\n"
                             "  <ProductLevel>L1A</ProductLevel>\n"
                             "  <ReceiveDate>2022-11-21</ReceiveDate>\n"
                             "  <ReceiveTime>10:23:45</ReceiveTime>\n"
                             "  <OrbitID>08871</OrbitID>\n"
                             "  <PixelSizeX>3.0</PixelSizeX>\n"
                             "  <PolarizationMode>HH/HV</PolarizationMode>\n"
                             "  <OrbitDirection>DESCENDING</OrbitDirection>\n"
                             "</MetaInfo>\n"));
    const QString output = tmp.path() + "/gf3-stack.tif";

    const auto plan =
        sicnu::operators::rs::planCnProductImport((base + ".tiff").toStdString(), "gaofen3_sar_product");
    REQUIRE(plan.identity.kindName == "gaofen3_sar_product");
    REQUIRE(plan.metadata.modality == "sar");
    REQUIRE(plan.metadata.polarizations.size() == 2);
    REQUIRE(plan.metadata.radiometricState == "digital_number");

    sicnu::operators::RSOperatorContext context;
    const Json::Value result = sicnu::operators::rs::executeCnProductImport(
        plan, output.toStdString(), {}, false, context);
    REQUIRE(QFile::exists(output));
    REQUIRE(result["productKind"].asString() == "gaofen3_sar_product");
    REQUIRE(result["radiometricState"].asString() == "digital_number");

    // The stacked product carries the declared SAR semantics verbatim.
    GDALDatasetH ds = GDALOpen(output.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);
    const char *productType = GDALGetMetadataItem(ds, "SICNU_PRODUCT_TYPE", nullptr);
    const char *polarizations = GDALGetMetadataItem(ds, "SICNU_POLARIZATIONS", nullptr);
    const char *orbitDirection = GDALGetMetadataItem(ds, "SICNU_ORBIT_DIRECTION", nullptr);
    REQUIRE(productType != nullptr);
    REQUIRE(std::string(productType) == "gaofen3_sar_product");
    REQUIRE(polarizations != nullptr);
    REQUIRE(std::string(polarizations) == "HH,HV");
    REQUIRE(orbitDirection != nullptr);
    REQUIRE(std::string(orbitDirection) == "DESCENDING");
    GDALClose(ds);

    // A pinned family filter refuses cross-family inputs.
    bool threw = false;
    try {
        (void)sicnu::operators::rs::planCnProductImport((base + ".tiff").toStdString(),
                                                        "zy3_product");
    } catch (const sicnu::operators::RSOperatorError &error) {
        threw = true;
        REQUIRE(error.code() == sicnu::operators::ErrorCode::InvalidInputData);
    }
    REQUIRE(threw);
}

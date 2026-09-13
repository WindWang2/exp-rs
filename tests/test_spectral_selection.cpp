// test_spectral_selection.cpp — rs:spectral_band_select + rs:library_select
// operator contracts (Hyperspectral Platform 10.0, work packages F/G).
//
// band_select: wavelength window with unit normalization (um -> nm),
// bad-band exclusion ranges, empty-selection and ambiguous-mode refusals,
// metadata propagation.
// library_select: validated-library load, material filter, near-duplicate QA,
// license echo, and registry discovery of both operators.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/algorithms/spectral_library.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_spectral_selection";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

/// 4-band raster with WAVELENGTH (um) + FWHM (nm) metadata, 2x2 pixels.
QString writeWavelengthRaster(const QString &path)
{
    std::vector<std::vector<float>> bands(4, std::vector<float>(4, 0.5f));
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    if (!writeGdalOutput(path, 2, 2, bands, gt, "EPSG:32648", &err))
        return err;
    GdalDatasetWrapper ds;
    if (!ds.open(path))
        return QStringLiteral("cannot reopen");
    GDALDatasetH handle = ds.dataset();
    const double centersUm[] = { 0.5, 0.65, 0.85, 1.65 };
    const double fwhmNm[] = { 20.0, 30.0, 40.0, 90.0 };
    for (int b = 1; b <= 4; ++b)
    {
        GDALRasterBandH band = GDALGetRasterBand(handle, b);
        GDALSetMetadataItem(band, "WAVELENGTH",
                            QString::number(centersUm[b - 1], 'f', 4).toUtf8().constData(),
                            nullptr);
        GDALSetMetadataItem(band, "WAVELENGTH_UNITS", "um", nullptr);
        GDALSetMetadataItem(band, "FWHM",
                            QString::number(fwhmNm[b - 1], 'f', 2).toUtf8().constData(),
                            nullptr);
        GDALSetMetadataItem(band, "FWHM_UNITS", "nm", nullptr);
    }
    ds.close();
    return {};
}

/// Minimal strictly-valid spectral library: two near-identical vegetation
/// entries (near-duplicate QA) and one water entry; 5-band shared grid.
QJsonObject testLibrary()
{
    QJsonObject root;
    root["id"] = "test-lib";
    QJsonArray wl{ 400.0, 500.0, 600.0, 700.0, 800.0 };
    QJsonArray fwhm{ 10.0, 10.0, 10.0, 10.0, 10.0 };
    root["wavelengths"] = wl;
    root["fwhm"] = fwhm;

    QJsonArray entries;
    auto makeEntry = [](const QString &name, const QString &material,
                        QJsonArray spectrum) {
        QJsonObject e;
        e["id"] = name.toLower().replace(' ', '-');
        e["name"] = name;
        e["material"] = material;
        e["spectrum"] = spectrum;
        e["source"] = "test";
        e["license"] = "CC0-1.0";
        e["citation"] = "test citation";
        e["synthetic"] = true;
        e["derivation"] = "test fixture";
        return e;
    };
    QJsonArray veg1{ 0.05, 0.06, 0.40, 0.45, 0.30 };
    QJsonArray veg2{ 0.05, 0.06, 0.401, 0.451, 0.301 }; // SAM angle ~0.1 deg
    QJsonArray water{ 0.30, 0.25, 0.05, 0.02, 0.01 };
    entries.append(makeEntry("Veg A", "vegetation", veg1));
    entries.append(makeEntry("Veg B", "vegetation", veg2));
    entries.append(makeEntry("Water", "water", water));
    root["entries"] = entries;
    return root;
}

QString writeLibrary(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("cannot open for write");
    const QByteArray bytes = QJsonDocument(testLibrary()).toJson(QJsonDocument::Compact);
    if (f.write(bytes) != bytes.size())
        return QStringLiteral("short write");
    return {};
}

} // namespace

TEST_CASE("Band select picks by wavelength window with nm normalization",
          "[spectral][band_select]")
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString input = dir.filePath("wl.tif");
    REQUIRE(writeWavelengthRaster(input).isEmpty());

    auto op = RSOperatorRegistry::instance().create("rs:spectral_band_select");
    REQUIRE(op != nullptr);

    Json::Value params;
    params["input"] = input.toStdString();
    params["output"] = dir.filePath("subset.tif").toStdString();
    params["wavelengthMin"] = 600.0;  // nm
    params["wavelengthMax"] = 900.0;  // nm

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["bands"].asInt() == 2); // 650 + 850
    CHECK(result["sourceBands"].size() == 2);

    // Kept bands carry nm-normalized WAVELENGTH metadata.
    GdalDatasetWrapper out;
    REQUIRE(out.open(QString::fromStdString(result["output"].asString())));
    REQUIRE(out.bandCount() == 2);
    GDALDatasetH handle = out.dataset();
    GDALRasterBandH b1 = GDALGetRasterBand(handle, 1);
    const char *wl = GDALGetMetadataItem(b1, "WAVELENGTH", nullptr);
    const char *units = GDALGetMetadataItem(b1, "WAVELENGTH_UNITS", nullptr);
    REQUIRE(wl != nullptr);
    REQUIRE(units != nullptr);
    CHECK(QString::fromLatin1(units) == QStringLiteral("nm"));
    CHECK(QString::fromLatin1(wl).toDouble() == Approx(650.0).margin(0.01));
}

TEST_CASE("Band select excludes bad bands and refuses empty selections",
          "[spectral][band_select]")
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString input = dir.filePath("wl.tif");
    REQUIRE(writeWavelengthRaster(input).isEmpty());

    auto op = RSOperatorRegistry::instance().create("rs:spectral_band_select");
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;

    SECTION("excludeRanges drops the 800-1000 nm band")
    {
        Json::Value params;
        params["input"] = input.toStdString();
        params["output"] = dir.filePath("clean.tif").toStdString();
        Json::Value ranges(Json::arrayValue);
        Json::Value range(Json::objectValue);
        range["minNm"] = 800.0;
        range["maxNm"] = 1000.0;
        ranges.append(range);
        params["excludeRanges"] = ranges;
        Json::Value result = op->run(params, ctx);
        CHECK(result["bands"].asInt() == 3);
    }
    SECTION("window matching nothing refuses with coverage in the message")
    {
        Json::Value params;
        params["input"] = input.toStdString();
        params["output"] = dir.filePath("none.tif").toStdString();
        params["wavelengthMin"] = 3000.0;
        params["wavelengthMax"] = 3100.0;
        bool threw = false;
        try
        {
            (void)op->run(params, ctx);
        }
        catch (const RSOperatorError &e)
        {
            threw = true;
            CHECK(e.message().find("No band center") != std::string::npos);
        }
        CHECK(threw);
    }
    SECTION("ambiguous modes refuse")
    {
        Json::Value params;
        params["input"] = input.toStdString();
        params["output"] = dir.filePath("amb.tif").toStdString();
        params["wavelengthMin"] = 600.0;
        params["wavelengthMax"] = 900.0;
        Json::Value ranges(Json::arrayValue);
        Json::Value range(Json::objectValue);
        range["minNm"] = 400.0;
        range["maxNm"] = 450.0;
        ranges.append(range);
        params["excludeRanges"] = ranges;
        bool threw = false;
        try
        {
            (void)op->run(params, ctx);
        }
        catch (const RSOperatorError &e)
        {
            threw = true;
            CHECK(e.message().find("Ambiguous") != std::string::npos);
        }
        CHECK(threw);
    }
}

TEST_CASE("Library select filters materials and reports near-duplicates",
          "[spectral][library_select]")
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString libraryPath = dir.filePath("library.json");
    REQUIRE(writeLibrary(libraryPath).isEmpty());

    auto op = RSOperatorRegistry::instance().create("rs:library_select");
    REQUIRE(op != nullptr);

    Json::Value params;
    params["libraryPath"] = libraryPath.toStdString();
    params["output"] = dir.filePath("subset.json").toStdString();
    Json::Value materials(Json::arrayValue);
    materials.append("vegetation");
    params["materials"] = materials;
    params["nearDuplicateAngleDeg"] = 0.5;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["entries"].asInt() == 2);
    CHECK(result["synthetic"].asBool());
    CHECK_FALSE(result["measured"].asBool());
    CHECK(std::string(result["license"].asString()) == "CC0-1.0");
    CHECK(result["nearDuplicatePairs"].size() == 1);

    // The subset is a valid library the operators can consume again.
    SpectralLibrary::Library subset;
    QString err;
    REQUIRE(SpectralLibrary::Library::loadValidated(
        QString::fromStdString(result["output"].asString()), &subset, &err));
    CHECK(subset.entries.size() == 2);
}

TEST_CASE("Library select refuses unknown materials and invalid libraries",
          "[spectral][library_select]")
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString libraryPath = dir.filePath("library.json");
    REQUIRE(writeLibrary(libraryPath).isEmpty());

    auto op = RSOperatorRegistry::instance().create("rs:library_select");
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;

    SECTION("unknown material filter")
    {
        Json::Value params;
        params["libraryPath"] = libraryPath.toStdString();
        params["output"] = dir.filePath("none.json").toStdString();
        Json::Value materials(Json::arrayValue);
        materials.append("urban_sprawl");
        params["materials"] = materials;
        bool threw = false;
        try
        {
            (void)op->run(params, ctx);
        }
        catch (const RSOperatorError &e)
        {
            threw = true;
            CHECK(e.message().find("matched no entries") != std::string::npos);
        }
        CHECK(threw);
    }
    SECTION("unvalidated library refuses")
    {
        // Strip the provenance fields -> loadValidated refuses at load.
        QJsonObject lib = testLibrary();
        QJsonArray entries = lib["entries"].toArray();
        QJsonObject entry = entries.at(0).toObject();
        entry.remove("license");
        entries[0] = entry;
        lib["entries"] = entries;
        const QString badPath = dir.filePath("bad.json");
        QFile f(badPath);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(lib).toJson(QJsonDocument::Compact));
        f.close();

        Json::Value params;
        params["libraryPath"] = badPath.toStdString();
        params["output"] = dir.filePath("out.json").toStdString();
        bool threw = false;
        try
        {
            (void)op->run(params, ctx);
        }
        catch (const RSOperatorError &e)
        {
            threw = true;
        }
        CHECK(threw);
    }
}

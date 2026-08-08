/***************************************************************************
 * rs_modis_import_operator.cpp
 ***************************************************************************/
#include "rs_modis_import_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/satellite_products.h"

#include <QString>
#include <QStringList>

namespace sicnu::operators::rs {

using namespace params;

namespace {

QStringList parseBandList(const Json::Value& p)
{
    QStringList names;
    if (!p.isMember("bands") || !p["bands"].isArray() || p["bands"].empty()) {
        // Prefer surface reflectance short names when caller does not specify;
        // stackToGeoTiff falls back to all non-QA bands if none match.
        return SatelliteProducts::defaultModisReflectanceBands();
    }
    for (Json::ArrayIndex i = 0; i < p["bands"].size(); ++i) {
        if (p["bands"][i].isString())
            names << QString::fromStdString(p["bands"][i].asString());
    }
    return names;
}

} // namespace

Json::Value RsModisImportOperator::schema() const
{
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeStringParam(
        "input", "Path to MODIS .hdf/.h5, MOD*.tif, or product directory", "");
    props["output"] = makeOutputParam("output", "Output multi-band GeoTIFF", "tif");
    Json::Value bands = makeStringParam("bands", "Optional band/subdataset names", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "string";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Stacked GeoTIFF", "tif");
    outputs["productId"] = makeStringParam("productId", "MODIS product id", "");
    outputs["bandCount"] = makeIntegerParam("bandCount", "Number of stacked bands", 0);
    outputs["tileH"] = makeIntegerParam("tileH", "MODIS horizontal tile index", -1);
    outputs["tileV"] = makeIntegerParam("tileV", "MODIS vertical tile index", -1);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsModisImportOperator::metadata() const
{
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("modis");
    meta["tags"].append("import");
    meta["tags"].append("data-format");
    meta["tags"].append("hdf");
    meta["purpose"] =
        "Convert a MODIS HDF/GeoTIFF product into analysis-ready multi-band GeoTIFF";
    meta["prerequisites"].append(
        "GDAL with HDF4 and/or HDF5 for NASA .hdf; GeoTIFF exports always work");
    meta["workflowHints"].append(
        "Import then rs:modis_georeference to EPSG:4326 if needed for GIS overlays");
    meta["workflowHints"].append(
        "Stack sur_refl_b01/b02 then rs:spectral_index for MODIS NDVI teaching labs");
    return meta;
}

Json::Value RsModisImportOperator::executionEstimate() const
{
    // FullRaster (default policy). Band stacking copies one full Float32 band at
    // a time: peak RAM is a single 1024x1024 band buffer plus fixed overhead.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 12582912; // 1 x 1024x1024 Float32 band + 8 MiB fixed
    return est;
}

Json::Value RsModisImportOperator::run(const Json::Value& p, RSOperatorContext& context)
{
    const std::string inputPath = requireString(p, "input");
    const std::string outputPath = requireString(p, "output");
    QStringList bandNames = parseBandList(p);

    context.reportProgress(0.05, "Discovering MODIS product");
    SatelliteProducts::ProductInfo product;
    QString err;
    if (!SatelliteProducts::discoverModis(QString::fromStdString(inputPath), &product, &err)) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              err.isEmpty() ? "MODIS discovery failed" : err.toStdString());
    }

    // If preferred sur_refl names are missing, stack all non-QA bands.
    {
        QStringList missing;
        for (const QString& n : bandNames) {
            bool found = false;
            for (const auto& b : product.bands) {
                if (b.name.contains(n, Qt::CaseInsensitive)
                    || n.contains(b.name, Qt::CaseInsensitive)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                missing << n;
        }
        if (missing.size() == bandNames.size())
            bandNames.clear(); // selectBands → all non-QA
    }

    context.logInfo("MODIS product: " + product.productId.toStdString()
                    + " (" + product.spacecraft.toStdString() + ", "
                    + std::to_string(product.bands.size()) + " bands/subdatasets"
                    + (product.modisTileH >= 0
                           ? ", h" + std::to_string(product.modisTileH) + "v"
                                 + std::to_string(product.modisTileV)
                           : "")
                    + ")");
    context.throwIfCancelled();

    context.reportProgress(0.15, "Stacking bands to GeoTIFF");
    const bool ok = SatelliteProducts::stackToGeoTiff(
        product, bandNames, QString::fromStdString(outputPath), &err,
        [&](double frac, const QString& msg) {
            context.reportProgress(0.15 + 0.8 * frac, msg.toStdString());
            context.throwIfCancelled();
        });

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              err.isEmpty() ? "Failed to stack MODIS bands" : err.toStdString());
    }

    context.reportProgress(1.0, "MODIS import complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["productId"] = product.productId.toStdString();
    result["spacecraft"] = product.spacecraft.toStdString();
    result["processingLevel"] = product.processingLevel.toStdString();
    result["acquisitionDate"] = product.acquisitionDate.toStdString();
    result["tileH"] = product.modisTileH;
    result["tileV"] = product.modisTileV;
    result["bandCount"] = bandNames.isEmpty()
                              ? static_cast<int>(product.bands.size())
                              : static_cast<int>(bandNames.size());
    result["bands"] = Json::Value(Json::arrayValue);
    for (const QString& b : bandNames)
        result["bands"].append(b.toStdString());
    return result;
}

} // namespace sicnu::operators::rs

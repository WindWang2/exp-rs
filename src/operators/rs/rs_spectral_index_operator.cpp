/***************************************************************************
 * rs_spectral_index_operator.cpp  —  Spectral index RSOperator
 ***************************************************************************/
#include "rs_spectral_index_operator.h"

#include "data/band_role.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_indices = {
    "NDVI", "EVI", "SAVI", "NDWI", "NDBI", "MNDWI"
};

} // anonymous namespace

Json::Value RsSpectralIndexOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output single-band index raster", "tif");
    props["index"] = makeEnumParam("index", "Spectral index to compute", s_indices, "NDVI");
    props["nir"] = makeIntegerParam("nir", "1-based NIR band number (optional; when omitted, resolved from the input's product band roles)", 4);
    props["red"] = makeIntegerParam("red", "1-based Red band number (optional; when omitted, resolved from the input's product band roles)", 3);
    props["green"] = makeIntegerParam("green", "1-based Green band number (optional; when omitted, resolved from the input's product band roles)", 2);
    props["blue"] = makeIntegerParam("blue", "1-based Blue band number (optional; when omitted, resolved from the input's product band roles)", 1);
    props["swir"] = makeIntegerParam("swir", "1-based SWIR band number (optional; when omitted, resolved from the input's product band roles)", 5);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["index"] = makeStringParam("index", "Computed index name", "");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "index"});
    return root;
}

Json::Value RsSpectralIndexOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("ndvi");
    meta["tags"].append("evi");
    meta["tags"].append("vegetation");
    meta["purpose"] = "Derive vegetation, water, or built-up indices from multispectral imagery.";
    meta["prerequisites"].append("Input raster must have sufficient bands for the selected index.");
    meta["workflowHints"].append("Apply atmospheric correction before computing indices for best results.");
    meta["limitations"].append("Band numbers are 1-based and must exist in the input raster. When a band "
                               "parameter is omitted, it is resolved from the input's SICNU_BAND_ROLE "
                               "product metadata (semantic band roles) instead of the positional default.");
    return meta;
}

Json::Value RsSpectralIndexOperator::executionEstimate() const {
    // FullRaster (base default): no preferred tile; the whole input raster is
    // resident. Typical input 1024x1024x4 float32 (~4 MiB/band); the worst-case
    // index (EVI) keeps 3 input bands + 1 output buffer in flight at once.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216;
    return est;
}

Json::Value RsSpectralIndexOperator::run(const Json::Value& params,
                                         RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string indexName = getEnum(params, "index", s_indices, "NDVI");

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    // Explicit band parameters win; when omitted, bands are resolved from the
    // input's semantic band roles (SICNU_BAND_ROLE product metadata).
    const bool hasNir = params.isMember("nir");
    const bool hasRed = params.isMember("red");
    const bool hasGreen = params.isMember("green");
    const bool hasBlue = params.isMember("blue");
    const bool hasSwir = params.isMember("swir");

    const int nirExplicit = getInt(params, "nir", 4);
    const int redExplicit = getInt(params, "red", 3);
    const int greenExplicit = getInt(params, "green", 2);
    const int blueExplicit = getInt(params, "blue", 1);
    const int swirExplicit = getInt(params, "swir", 5);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);
    }

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();

    // 1-based band number carrying @a role, or 0 when the input has no such
    // role (plain rasters without product metadata return 0).
    auto bandWithRole = [&](sicnu::data::BandRole role) {
        const QByteArray roleId = sicnu::data::bandRoleToString(role).toLatin1();
        for (int b = 1; b <= bandCount; ++b) {
            if (ds.bandMetadataItem(b, "SICNU_BAND_ROLE") == QLatin1String(roleId))
                return b;
        }
        return 0;
    };

    int nirBand = nirExplicit;
    int redBand = redExplicit;
    int greenBand = greenExplicit;
    int blueBand = blueExplicit;
    int swirBand = swirExplicit;
    bool anyHardcodedFallback = false;
    if (!hasNir) {
        nirBand = bandWithRole(sicnu::data::BandRole::NIR);
        if (nirBand <= 0) {
            nirBand = 4;
            anyHardcodedFallback = true;
        }
    }
    if (!hasRed) {
        redBand = bandWithRole(sicnu::data::BandRole::Red);
        if (redBand <= 0) {
            redBand = 3;
            anyHardcodedFallback = true;
        }
    }
    if (!hasGreen) {
        greenBand = bandWithRole(sicnu::data::BandRole::Green);
        if (greenBand <= 0) {
            greenBand = 2;
            anyHardcodedFallback = true;
        }
    }
    if (!hasBlue) {
        blueBand = bandWithRole(sicnu::data::BandRole::Blue);
        if (blueBand <= 0) {
            blueBand = 1;
            anyHardcodedFallback = true;
        }
    }
    if (!hasSwir) {
        // NDBI/MNDWI conventionally use SWIR1; fall back to SWIR2.
        swirBand = bandWithRole(sicnu::data::BandRole::SWIR1);
        if (swirBand <= 0)
            swirBand = bandWithRole(sicnu::data::BandRole::SWIR2);
        if (swirBand <= 0) {
            swirBand = 5;
            anyHardcodedFallback = true;
        }
    }

    // P1: never silently assume a band ordering. When the input carries no
    // SICNU_BAND_ROLE metadata and the caller did not pick bands explicitly,
    // say so — the hardcoded fallback assumes the common multi-spectral band
    // order (NIR=4, Red=3, Green=2, Blue=1, SWIR1=5).
    if (anyHardcodedFallback) {
        context.logWarning(
            "No semantic band roles (SICNU_BAND_ROLE) on the input and no "
            "explicit band parameters; assuming the conventional band order "
            "(NIR=4, Red=3, Green=2, Blue=1, SWIR1=5). Verify with "
            "describe_dataset or pass nir/red/green/blue/swir explicitly.");
    }

    auto validateBand = [&](int bandNum, const std::string& label) {
        if (bandNum < 1 || bandNum > bandCount) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  label + " band " + std::to_string(bandNum) +
                                      " is out of range (1-" + std::to_string(bandCount) + ")");
        }
    };

    context.logInfo("Computing " + indexName + " from " + inputPath);

    std::vector<float> nir, red, green, blue, swir, out;

    auto readBand = [&](int bandNum, std::vector<float>& buffer) {
        buffer.resize(static_cast<size_t>(width) * height);
        if (!ds.readBandData(bandNum, buffer.data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(bandNum));
        }
    };

    context.reportProgress(0.1, "Reading input bands");

    bool ok = false;
    if (indexName == "NDVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        readBand(nirBand, nir);
        readBand(redBand, red);
        out.resize(nir.size());
        ok = SpectralIndices::ndvi(nir.data(), red.data(), out.data(), out.size());
    } else if (indexName == "EVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        validateBand(blueBand, "Blue");
        readBand(nirBand, nir);
        readBand(redBand, red);
        readBand(blueBand, blue);
        out.resize(nir.size());
        ok = SpectralIndices::evi(nir.data(), red.data(), blue.data(), out.data(), out.size());
    } else if (indexName == "SAVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        readBand(nirBand, nir);
        readBand(redBand, red);
        out.resize(nir.size());
        ok = SpectralIndices::savi(nir.data(), red.data(), out.data(), out.size());
    } else if (indexName == "NDWI") {
        validateBand(greenBand, "Green");
        validateBand(nirBand, "NIR");
        readBand(greenBand, green);
        readBand(nirBand, nir);
        out.resize(green.size());
        ok = SpectralIndices::ndwi(green.data(), nir.data(), out.data(), out.size());
    } else if (indexName == "NDBI") {
        validateBand(swirBand, "SWIR");
        validateBand(nirBand, "NIR");
        readBand(swirBand, swir);
        readBand(nirBand, nir);
        out.resize(swir.size());
        ok = SpectralIndices::ndbi(swir.data(), nir.data(), out.data(), out.size());
    } else if (indexName == "MNDWI") {
        validateBand(greenBand, "Green");
        validateBand(swirBand, "SWIR");
        readBand(greenBand, green);
        readBand(swirBand, swir);
        out.resize(green.size());
        ok = SpectralIndices::mndwi(green.data(), swir.data(), out.data(), out.size());
    }

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Spectral index computation failed");
    }

    context.throwIfCancelled();
    context.reportProgress(0.7, "Writing output raster");

    std::vector<std::vector<float>> bands = {std::move(out)};
    QString errorMessage;
    if (!writeGdalOutput(QString::fromStdString(outputPath), width, height, bands,
                         ds.geoTransform(), ds.projection(), &errorMessage)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + errorMessage.toStdString());
    }

    ds.close();

    context.reportProgress(1.0, indexName + " complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["index"] = indexName;
    result["width"] = width;
    result["height"] = height;
    return result;
}

} // namespace sicnu::operators::rs

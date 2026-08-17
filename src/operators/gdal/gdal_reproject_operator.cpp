/***************************************************************************
 * gdal_reproject_operator.cpp  —  GDAL raster reprojection
 ***************************************************************************/
#include "gdal_reproject_operator.h"
#include "gdal_operator_utils.h"

#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace sicnu::operators::gdal {

Json::Value GdalReprojectOperator::schema() const {
    using namespace schema;
    using namespace util;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster to reproject");
    props["output"] = makeOutputParam("output", "Output reprojected raster", "tif");
    props["dstCrs"] = makeStringParam("dstCrs",
                                      "Target CRS (e.g. EPSG:3857 or WKT)",
                                      "");
    props["srcCrs"] = makeStringParam("srcCrs",
                                      "Override source CRS (optional)",
                                      "");
    props["srcCrs"]["required"] = false;
    props["resampling"] = makeEnumParam("resampling", "Resampling method",
                                        resamplingNames(), "bilinear");
    props["targetResolution"] = makeNumberParam("targetResolution",
                                                "Output pixel size in target CRS units (optional)",
                                                0.0);
    props["targetResolution"]["required"] = false;
    props["reference"] = makeRasterParam(
        "reference",
        "Reference raster whose grid (CRS, pixel size, extent) the output aligns to "
        "(optional; overrides dstCrs and targetResolution)");
    props["reference"]["required"] = false;
    props["nodata"] = makeNumberParam("nodata", "Output no-data value (optional)", 0.0);
    props["nodata"]["required"] = false;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Reprojected GeoTIFF", "tif");
    outputs["width"] = makeIntegerParam("width", "Output width in pixels", 0);
    outputs["height"] = makeIntegerParam("height", "Output height in pixels", 0);

    Json::Value root = makeRootSchema("gdal:reproject", description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value GdalReprojectOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "gdal";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("reproject");
    meta["tags"].append("warp");
    meta["tags"].append("crs");
    meta["tags"].append("geometry");
    meta["purpose"] = "Change the coordinate reference system of a raster";
    meta["useCases"] = Json::Value(Json::arrayValue);
    meta["useCases"].append("Align multi-source imagery to a common CRS before fusion");
    meta["useCases"].append("Prepare rasters for web mercator display");
    return meta;
}

Json::Value GdalReprojectOperator::run(const Json::Value& params,
                                       RSOperatorContext& context) {
    using namespace util;

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string reference = getString(params, "reference", "");
    // dstCrs is required unless a reference raster supplies the target grid.
    const std::string dstCrs = reference.empty()
        ? requireString(params, "dstCrs")
        : getString(params, "dstCrs", "");
    const std::string srcCrs = getString(params, "srcCrs", "");
    const std::string resampling = getEnum(params, "resampling", resamplingNames(), "bilinear");
    const double targetResolution = getDouble(params, "targetResolution", 0.0);
    const bool hasNodata = hasNumber(params, "nodata");
    const double nodata = hasNodata ? getDouble(params, "nodata", 0.0) : 0.0;

    if (reference.empty() && dstCrs.empty()) {
        throw RSOperatorError(ErrorCode::MissingRequiredParameter,
                              "dstCrs must not be empty (or pass `reference` to align to its grid)");
    }

    // Grid harmonization: when a reference raster is given, derive the target
    // CRS, pixel size, and extent from it so the output lands on the reference
    // grid exactly (the shared grid-alignment service's workflow seam).
    double alignResX = 0.0, alignResY = 0.0;
    std::array<double, 4> alignExtent{};
    bool hasAlignExtent = false;
    std::string effectiveDstCrs = dstCrs;
    if (!reference.empty()) {
        GdalDatasetWrapper refDs;
        if (!refDs.open(QString::fromStdString(reference))) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to open reference raster: " + reference);
        }
        const std::string refCrs = refDs.projection().toStdString();
        if (refCrs.empty()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Reference raster has no CRS: " + reference);
        }
        const auto gt = refDs.geoTransform();
        alignResX = std::abs(gt[1]);
        alignResY = std::abs(gt[5]);
        if (alignResX <= 0.0 || alignResY <= 0.0) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Reference raster has a degenerate pixel size: " + reference);
        }
        // Rotation-aware bounding box of the reference footprint.
        const int rw = refDs.width();
        const int rh = refDs.height();
        const std::array<double, 4> cx = {gt[0],
                                          gt[0] + rw * gt[1],
                                          gt[0] + rw * gt[1] + rh * gt[2],
                                          gt[0] + rh * gt[2]};
        const std::array<double, 4> cy = {gt[3],
                                          gt[3] + rw * gt[4],
                                          gt[3] + rw * gt[4] + rh * gt[5],
                                          gt[3] + rh * gt[5]};
        alignExtent[0] = *std::min_element(cx.begin(), cx.end());
        alignExtent[2] = *std::max_element(cx.begin(), cx.end());
        alignExtent[1] = *std::min_element(cy.begin(), cy.end());
        alignExtent[3] = *std::max_element(cy.begin(), cy.end());
        hasAlignExtent = true;
        if (!dstCrs.empty() && dstCrs != refCrs) {
            context.logWarning("`reference` overrides `dstCrs` (" + dstCrs + ") with " + refCrs);
        }
        effectiveDstCrs = refCrs;
    }

    std::vector<std::string> options;
    appendGeoTiffDefaults(options, resampling);

    options.emplace_back("-t_srs");
    options.emplace_back(effectiveDstCrs);

    if (!srcCrs.empty()) {
        options.emplace_back("-s_srs");
        options.emplace_back(srcCrs);
    }

    if (!reference.empty()) {
        options.emplace_back("-tr");
        options.emplace_back(fmtDouble(alignResX));
        options.emplace_back(fmtDouble(alignResY));
        options.emplace_back("-te");
        options.emplace_back(fmtDouble(alignExtent[0]));
        options.emplace_back(fmtDouble(alignExtent[1]));
        options.emplace_back(fmtDouble(alignExtent[2]));
        options.emplace_back(fmtDouble(alignExtent[3]));
    } else if (targetResolution > 0.0) {
        options.emplace_back("-tr");
        options.emplace_back(fmtDouble(targetResolution));
        options.emplace_back(fmtDouble(targetResolution));
    }

    if (hasNodata) {
        options.emplace_back("-dstnodata");
        options.emplace_back(fmtDouble(nodata));
    }

    auto [width, height] = runGdalWarp(inputPath, outputPath, options, context,
                                       "GDAL reproject");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["width"] = width;
    result["height"] = height;
    result["dstCrs"] = effectiveDstCrs;
    result["aligned"] = !reference.empty();
    return result;
}

} // namespace sicnu::operators::gdal

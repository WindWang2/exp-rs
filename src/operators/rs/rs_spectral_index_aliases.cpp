/***************************************************************************
 * rs_spectral_index_aliases.cpp  —  Atomic spectral index operators
 ***************************************************************************/
#include "rs_spectral_index_aliases.h"
#include "rs_spectral_index_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

namespace sicnu::operators::rs {

using namespace params;

// ============================================================================
// RsNdviOperator
// ============================================================================

Json::Value RsNdviOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output NDVI raster", "tif");
    props["nir"] = makeIntegerParam("nir", "1-based NIR band number (optional; auto-resolved from band roles)", 4);
    props["red"] = makeIntegerParam("red", "1-based Red band number (optional; auto-resolved from band roles)", 3);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["index"] = makeStringParam("index", "Computed index name (NDVI)", "NDVI");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsNdviOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("ndvi");
    meta["tags"].append("vegetation");
    meta["purpose"] = "Assess live green vegetation coverage and vigor.";
    meta["workflowHints"].append("Apply atmospheric correction before computing NDVI for best results.");
    meta["limitations"].append("Band numbers are resolved from SICNU_BAND_ROLE when omitted.");
    meta["facadeOf"] = "spectral_index";
    return meta;
}

Json::Value RsNdviOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216;
    return est;
}

Json::Value RsNdviOperator::run(const Json::Value& params, RSOperatorContext& context) {
    return spectral_index_detail::runSpectralIndexCore("NDVI", params, context);
}

// ============================================================================
// RsEviOperator
// ============================================================================

Json::Value RsEviOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output EVI raster", "tif");
    props["nir"] = makeIntegerParam("nir", "1-based NIR band number (optional; auto-resolved from band roles)", 4);
    props["red"] = makeIntegerParam("red", "1-based Red band number (optional; auto-resolved from band roles)", 3);
    props["blue"] = makeIntegerParam("blue", "1-based Blue band number (optional; auto-resolved from band roles)", 1);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["index"] = makeStringParam("index", "Computed index name (EVI)", "EVI");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsEviOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("evi");
    meta["tags"].append("vegetation");
    meta["purpose"] = "Enhanced vegetation index optimized for high-biomass canopy regions.";
    meta["workflowHints"].append("Requires NIR, Red, and Blue bands with surface reflectance values.");
    meta["limitations"].append("Band numbers are resolved from SICNU_BAND_ROLE when omitted.");
    meta["facadeOf"] = "spectral_index";
    return meta;
}

Json::Value RsEviOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 20971520;
    return est;
}

Json::Value RsEviOperator::run(const Json::Value& params, RSOperatorContext& context) {
    return spectral_index_detail::runSpectralIndexCore("EVI", params, context);
}

// ============================================================================
// RsNdwiOperator
// ============================================================================

Json::Value RsNdwiOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output NDWI raster", "tif");
    props["green"] = makeIntegerParam("green", "1-based Green band number (optional; auto-resolved from band roles)", 2);
    props["nir"] = makeIntegerParam("nir", "1-based NIR band number (optional; auto-resolved from band roles)", 4);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["index"] = makeStringParam("index", "Computed index name (NDWI)", "NDWI");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsNdwiOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("ndwi");
    meta["tags"].append("water");
    meta["purpose"] = "Delineate open water features and monitor water body extents.";
    meta["workflowHints"].append("Threshold > 0 generally indicates open water surfaces.");
    meta["limitations"].append("Band numbers are resolved from SICNU_BAND_ROLE when omitted.");
    meta["facadeOf"] = "spectral_index";
    return meta;
}

Json::Value RsNdwiOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216;
    return est;
}

Json::Value RsNdwiOperator::run(const Json::Value& params, RSOperatorContext& context) {
    return spectral_index_detail::runSpectralIndexCore("NDWI", params, context);
}

// ============================================================================
// RsSaviOperator
// ============================================================================

Json::Value RsSaviOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output SAVI raster", "tif");
    props["nir"] = makeIntegerParam("nir", "1-based NIR band number (optional; auto-resolved from band roles)", 4);
    props["red"] = makeIntegerParam("red", "1-based Red band number (optional; auto-resolved from band roles)", 3);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["index"] = makeStringParam("index", "Computed index name (SAVI)", "SAVI");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSaviOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("savi");
    meta["tags"].append("vegetation");
    meta["purpose"] = "Vegetation index adjusted for soil background reflectance in arid/semi-arid regions.";
    meta["workflowHints"].append("Uses canopy background adjustment factor L = 0.5.");
    meta["limitations"].append("Band numbers are resolved from SICNU_BAND_ROLE when omitted.");
    meta["facadeOf"] = "spectral_index";
    return meta;
}

Json::Value RsSaviOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216;
    return est;
}

Json::Value RsSaviOperator::run(const Json::Value& params, RSOperatorContext& context) {
    return spectral_index_detail::runSpectralIndexCore("SAVI", params, context);
}

// ============================================================================
// RsNdbiOperator
// ============================================================================

Json::Value RsNdbiOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output NDBI raster", "tif");
    props["swir"] = makeIntegerParam("swir", "1-based SWIR band number (optional; auto-resolved from band roles)", 5);
    props["nir"] = makeIntegerParam("nir", "1-based NIR band number (optional; auto-resolved from band roles)", 4);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["index"] = makeStringParam("index", "Computed index name (NDBI)", "NDBI");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsNdbiOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("ndbi");
    meta["tags"].append("urban");
    meta["tags"].append("built-up");
    meta["purpose"] = "Map built-up urban and impervious surface extents.";
    meta["workflowHints"].append("Positive values correlate with high built-up density.");
    meta["limitations"].append("Band numbers are resolved from SICNU_BAND_ROLE when omitted.");
    meta["facadeOf"] = "spectral_index";
    return meta;
}

Json::Value RsNdbiOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216;
    return est;
}

Json::Value RsNdbiOperator::run(const Json::Value& params, RSOperatorContext& context) {
    return spectral_index_detail::runSpectralIndexCore("NDBI", params, context);
}

// ============================================================================
// RsMndwiOperator
// ============================================================================

Json::Value RsMndwiOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output MNDWI raster", "tif");
    props["green"] = makeIntegerParam("green", "1-based Green band number (optional; auto-resolved from band roles)", 2);
    props["swir"] = makeIntegerParam("swir", "1-based SWIR band number (optional; auto-resolved from band roles)", 5);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["index"] = makeStringParam("index", "Computed index name (MNDWI)", "MNDWI");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsMndwiOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("mndwi");
    meta["tags"].append("water");
    meta["purpose"] = "Modified Normalized Difference Water Index for enhanced water extraction in urban environments.";
    meta["workflowHints"].append("Replaces NIR with SWIR to significantly suppress built-up noise compared to NDWI.");
    meta["limitations"].append("Band numbers are resolved from SICNU_BAND_ROLE when omitted.");
    meta["facadeOf"] = "spectral_index";
    return meta;
}

Json::Value RsMndwiOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216;
    return est;
}

Json::Value RsMndwiOperator::run(const Json::Value& params, RSOperatorContext& context) {
    return spectral_index_detail::runSpectralIndexCore("MNDWI", params, context);
}

} // namespace sicnu::operators::rs

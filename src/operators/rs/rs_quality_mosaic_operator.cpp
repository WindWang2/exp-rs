/***************************************************************************
 * rs_quality_mosaic_operator.cpp — F15 implementation (ADR 0163)
 ***************************************************************************/
#include "rs_quality_mosaic_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/fusion_quality_report.h"
#include "processing/algorithms/mosaic_balancing.h"
#include "processing/algorithms/mosaic_blend.h"
#include "processing/algorithms/mosaic_plan.h"
#include "processing/algorithms/mosaic_quality.h"
#include "processing/algorithms/mosaic_seamline.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/framework/resource_estimation.h"

#include <QFile>
#include <QString>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kTileSize = 512;
constexpr uint64_t kMaxOutputPixels = 200'000'000ULL;

struct InputSpec {
    std::string path;
    int priority = 0;
    bool hasCloud = false;
    double cloudFraction = 0.0;
    bool hasQuality = false;
    double quality = 0.0;
    bool hasTime = false;
    double timeDays = 0.0;
    bool hasView = false;
    double viewAngleDeg = 0.0;
    std::string cloudMaskPath;
};

InputSpec parseInput( const Json::Value &item )
{
    InputSpec spec;
    if ( item.isString() )
    {
        spec.path = item.asString();
        return spec;
    }
    if ( !item.isObject() )
        throw RSOperatorError( ErrorCode::TypeMismatch,
                               "inputs entries must be strings or objects" );
    if ( !item.isMember( "path" ) || !item["path"].isString() )
        throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                               "inputs object entries require a string 'path'" );
    spec.path = item["path"].asString();
    if ( item.isMember( "priority" ) && item["priority"].isInt() )
        spec.priority = item["priority"].asInt();
    if ( item.isMember( "cloudFraction" ) && item["cloudFraction"].isNumeric() )
    {
        spec.hasCloud = true;
        spec.cloudFraction = item["cloudFraction"].asDouble();
    }
    if ( item.isMember( "quality" ) && item["quality"].isNumeric() )
    {
        spec.hasQuality = true;
        spec.quality = item["quality"].asDouble();
    }
    if ( item.isMember( "timeDays" ) && item["timeDays"].isNumeric() )
    {
        spec.hasTime = true;
        spec.timeDays = item["timeDays"].asDouble();
    }
    if ( item.isMember( "viewAngleDeg" ) && item["viewAngleDeg"].isNumeric() )
    {
        spec.hasView = true;
        spec.viewAngleDeg = item["viewAngleDeg"].asDouble();
    }
    if ( item.isMember( "cloudMask" ) && item["cloudMask"].isString() )
        spec.cloudMaskPath = item["cloudMask"].asString();
    return spec;
}

/// Production sampler backed by open GDAL datasets. Reads plan-grid windows
/// and maps declared NoData to NaN (the convention every mosaic algorithm
/// module speaks).
class GdalOverlapSampler : public ::rs::mosaic::OverlapSampler
{
  public:
    GdalOverlapSampler( std::vector<GdalDatasetWrapper> *datasets,
                        const std::vector<float> &nodata,
                        const std::vector<::rs::mosaic::ScenePlanEntry> *planEntries )
        : datasets_( datasets ), nodata_( nodata ), entries_( planEntries )
    {
    }

    bool readGridWindow( int scene, int band, int64_t x0, int64_t y0, int64_t w, int64_t h,
                         std::vector<float> &out ) override
    {
        GdalDatasetWrapper &ds = ( *datasets_ )[static_cast<size_t>( scene )];
        const ::rs::mosaic::ScenePlanEntry &pe = ( *entries_ )[static_cast<size_t>( scene )];
        const ::rs::mosaic::ScenePlacement &pl = pe.placement;
        const ::rs::mosaic::SceneEntry &se = pe.scene;
        out.assign( static_cast<size_t>( w ) * h, std::numeric_limits<float>::quiet_NaN() );
        const int64_t sx0 = std::max<int64_t>( x0, pl.offsetX );
        const int64_t sy0 = std::max<int64_t>( y0, pl.offsetY );
        const int64_t sx1 = std::min<int64_t>( x0 + w, pl.offsetX + se.width );
        const int64_t sy1 = std::min<int64_t>( y0 + h, pl.offsetY + se.height );
        if ( sx0 >= sx1 || sy0 >= sy1 )
            return true;
        const int rw = static_cast<int>( sx1 - sx0 );
        const int rh = static_cast<int>( sy1 - sy0 );
        std::vector<float> buf( static_cast<size_t>( rw ) * rh );
        if ( !ds.readBandWindow( band + 1, static_cast<int>( sx0 - pl.offsetX ),
                                 static_cast<int>( sy0 - pl.offsetY ), rw, rh, buf.data() ) )
            return false;
        const float nd = nodata_[static_cast<size_t>( scene )];
        const bool hasNd = !std::isnan( nd );
        for ( int r = 0; r < rh; ++r )
        {
            for ( int c = 0; c < rw; ++c )
            {
                float v = buf[static_cast<size_t>( r ) * rw + c];
                if ( hasNd && v == nd )
                    v = std::numeric_limits<float>::quiet_NaN();
                out[static_cast<size_t>( sy0 - y0 + r ) * static_cast<size_t>( w ) +
                    static_cast<size_t>( sx0 - x0 + c )] = v;
            }
        }
        return true;
    }

  private:
    std::vector<GdalDatasetWrapper> *datasets_;
    const std::vector<float> &nodata_;
    const std::vector<::rs::mosaic::ScenePlanEntry> *entries_;
};

/// Aligned per-scene cloud mask sidecar; produces a [0,1] penalty window on
/// the plan grid. Misaligned sidecars fail closed at bind time.
class CloudMaskSidecar
{
  public:
    bool bind( const std::string &path, const std::array<double, 6> &sceneGt,
               int sceneW, int sceneH, int64_t offsetX, int64_t offsetY, QString *err )
    {
        if ( !ds_.open( QString::fromStdString( path ) ) )
        {
            *err = QStringLiteral( "cannot open cloud mask raster: " ) +
                   QString::fromStdString( path );
            return false;
        }
        const auto gt = ds_.geoTransform();
        for ( int i = 0; i < 6; ++i )
        {
            if ( std::abs( gt[i] - sceneGt[i] ) > 1e-9 * std::max( 1.0, std::abs( sceneGt[i] ) ) )
            {
                *err = QStringLiteral( "cloud mask is not co-registered with its scene "
                                       "(geotransform mismatch): " ) +
                       QString::fromStdString( path );
                return false;
            }
        }
        if ( ds_.width() != sceneW || ds_.height() != sceneH )
        {
            *err = QStringLiteral( "cloud mask size does not match its scene: " ) +
                   QString::fromStdString( path );
            return false;
        }
        offsetX_ = offsetX;
        offsetY_ = offsetY;
        bound_ = true;
        return true;
    }

    bool readPenalty( int64_t x0, int64_t y0, int64_t w, int64_t h, std::vector<float> &out )
    {
        out.assign( static_cast<size_t>( w ) * h, 0.0f );
        if ( !bound_ )
            return true;
        const int rw = static_cast<int>( w );
        const int rh = static_cast<int>( h );
        std::vector<float> buf( static_cast<size_t>( rw ) * rh );
        if ( !ds_.readBandWindow( 1, static_cast<int>( x0 - offsetX_ ),
                                  static_cast<int>( y0 - offsetY_ ), rw, rh, buf.data() ) )
            return false;
        for ( size_t i = 0; i < buf.size(); ++i )
        {
            const float v = buf[i];
            out[i] = std::isfinite( v ) ? std::clamp( v, 0.0f, 1.0f ) : 0.0f;
        }
        return true;
    }

    bool bound() const { return bound_; }

  private:
    GdalDatasetWrapper ds_;
    bool bound_ = false;
    int64_t offsetX_ = 0;
    int64_t offsetY_ = 0;
};

struct SeamEntry {
    ::rs::mosaic::SeamDecision decision;
    int negativeSideScene = -1; // scene index owning the negative (earlier) side
    int64_t rectX = 0, rectY = 0;
    int64_t cellW = 1, cellH = 1;

    /// Signed distance of a grid pixel from the seam center line, positive
    /// on the *positive* side of the seam geometry (right of a vertical
    /// seam / below a horizontal seam).
    double signedDistance( int64_t gx, int64_t gy ) const
    {
        const int64_t lx = gx - rectX;
        const int64_t ly = gy - rectY;
        if ( decision.orientation == ::rs::mosaic::SeamOrientation::Vertical )
        {
            const size_t row = decision.path.empty()
                                   ? 0
                                   : std::min( static_cast<size_t>( ly / cellH ),
                                               decision.path.size() - 1 );
            const double center = decision.path[row] * static_cast<double>( cellW ) +
                                  static_cast<double>( cellW ) / 2.0;
            return ( static_cast<double>( lx ) + 0.5 ) - center;
        }
        const size_t col = decision.path.empty()
                               ? 0
                               : std::min( static_cast<size_t>( lx / cellW ),
                                           decision.path.size() - 1 );
        const double center = decision.path[col] * static_cast<double>( cellH ) +
                              static_cast<double>( cellH ) / 2.0;
        return ( static_cast<double>( ly ) + 0.5 ) - center;
    }
};

} // namespace

Json::Value RsQualityMosaicOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    Json::Value inputs = makeStringParam("inputs",
                                         "Array of input raster paths or objects "
                                         "{path, priority, cloudFraction, quality, timeDays, "
                                         "viewAngleDeg, cloudMask}",
                                         "");
    inputs["type"] = "array";
    inputs["items"] = Json::Value(Json::objectValue);
    props["inputs"] = inputs;
    props["output"] = makeOutputParam("output", "Output mosaic GeoTIFF (tiled, deflate)", "tif");
    props["method"] = makeEnumParam("method", "Composite strategy",
                                    { "seamline", "quality" }, "seamline");
    props["bandCount"] = makeIntegerParam("bandCount", "Bands to mosaic (<= min input bands)", 0);
    props["balancing"] = makeStringParam(
        "balancing",
        "Object {enabled, referenceIndex, rejectPolicy(fail|drop), minGain, maxGain}; "
        "default enabled with fail-closed anomaly policy",
        "");
    props["seamline"] = makeStringParam(
        "seamline",
        "Object {enabled, weights{radiometric,gradient,cloud,edgeDistance}, maxCells}; "
        "default enabled",
        "");
    props["blending"] = makeStringParam(
        "blending", "Object {mode(none|feather), featherWidth}; default feather "
                    "(multiband pyramid blending is available as the kernel-level "
                    "mosaic_blend module)",
        "");
    props["qualityWeights"] = makeStringParam(
        "qualityWeights", "Object {cloud, quality, time, view}; default {0.5,0.25,0.15,0.10}",
        "");
    props["provenance"] = makeStringParam(
        "provenance", "Append a dominant-source band (0 = unfilled, i+1 = input i); "
                      "default true",
        "");
    props["reportOutput"] = makeStringParam("reportOutput", "JSON quality report path", "");
    props["overviews"] = makeStringParam("overviews",
                                         "Array of overview factors (e.g. [2,4,8]); "
                                         "default [2,4,8]", "");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Mosaic GeoTIFF", "tif");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);
    outputs["bandCount"] = makeIntegerParam("bandCount", "Mosaicked data bands", 0);
    outputs["inputCount"] = makeIntegerParam("inputCount", "Inputs mosaicked", 0);
    outputs["rejectedInputs"] = makeIntegerParam("rejectedInputs", "Rejected input indices", 0);
    outputs["seamCount"] = makeIntegerParam("seamCount", "Computed seamlines", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({ "inputs", "output" });
    return root;
}

Json::Value RsQualityMosaicOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    Json::Value tags(Json::arrayValue);
    tags.append("mosaic");
    tags.append("seamline");
    tags.append("quality");
    tags.append("composition");
    meta["tags"] = tags;
    meta["purpose"] =
        "Production quality mosaic: balancing, seamlines, blending, per-pixel provenance";
    meta["memoryPolicy"] = "streaming";
    meta["limitations"] =
        "Requires co-registered inputs on one CRS/pixel grid (reproject first); "
        "seam decisions use band 1; provenance band is Float32 (exact for <= 16.7M scenes)";
    return meta;
}

Json::Value RsQualityMosaicOperator::executionEstimate() const
{
    // Streaming: bounded by tile buffers (512^2 floats per plane, a handful of
    // planes) and seam bins (<= 512x512 cells); independent of mosaic extent.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = kTileSize;
    est["tileHeight"] = kTileSize;
    est["estimatedRamBytes"] = 64 * 1024 * 1024;
    return est;
}

Json::Value RsQualityMosaicOperator::estimateExecution(const Json::Value& params) const
{
    if (!params.isObject() || !params.isMember("inputs") || !params["inputs"].isArray()
        || params["inputs"].empty())
        return executionEstimate();
    ensureGdalInit();
    Json::Value est = executionEstimate();
    est["basis"] = "dynamic";
    return est;
}

Json::Value RsQualityMosaicOperator::run(const Json::Value& params, RSOperatorContext& context)
{
    if (!params.isMember("inputs") || !params["inputs"].isArray() || params["inputs"].empty())
        throw RSOperatorError(ErrorCode::MissingRequiredParameter,
                              "Parameter 'inputs' must be a non-empty array");
    const std::string outputPath = requireString(params, "output");
    if (outputPath.empty())
        throw RSOperatorError(ErrorCode::InvalidParameter, "Parameter 'output' must not be empty");

    ensureGdalInit();
    context.reportProgress(0.0, "Parsing quality mosaic parameters");

    // ---- Parameters ---------------------------------------------------------
    std::string method = "seamline";
    if ( params.isMember( "method" ) )
    {
        if ( !params["method"].isString() )
            throw RSOperatorError( ErrorCode::TypeMismatch, "'method' must be a string" );
        method = params["method"].asString();
        if ( method != "seamline" && method != "quality" )
            throw RSOperatorError( ErrorCode::InvalidEnumValue,
                                   "'method' must be 'seamline' or 'quality'" );
    }

    bool balanceEnabled = true;
    ::rs::mosaic::BalancingOptions balanceOptions;
    if ( params.isMember( "balancing" ) && params["balancing"].isObject() )
    {
        const Json::Value &b = params["balancing"];
        if ( b.isMember( "enabled" ) && b["enabled"].isBool() )
            balanceEnabled = b["enabled"].asBool();
        if ( b.isMember( "referenceIndex" ) && b["referenceIndex"].isInt() )
            balanceOptions.referenceScene = b["referenceIndex"].asInt();
        if ( b.isMember( "rejectPolicy" ) && b["rejectPolicy"].isString() )
        {
            const std::string p = b["rejectPolicy"].asString();
            if ( p == "drop" )
                balanceOptions.rejectPolicy = ::rs::mosaic::BalancingOptions::RejectPolicy::Drop;
            else if ( p != "fail" )
                throw RSOperatorError( ErrorCode::InvalidEnumValue,
                                       "balancing.rejectPolicy must be 'fail' or 'drop'" );
        }
        if ( b.isMember( "minGain" ) && b["minGain"].isNumeric() )
            balanceOptions.minGain = b["minGain"].asDouble();
        if ( b.isMember( "maxGain" ) && b["maxGain"].isNumeric() )
            balanceOptions.maxGain = b["maxGain"].asDouble();
    }

    bool seamEnabled = true;
    ::rs::mosaic::SeamCostWeights seamWeights;
    int seamMaxCells = 512;
    if ( params.isMember( "seamline" ) && params["seamline"].isObject() )
    {
        const Json::Value &s = params["seamline"];
        if ( s.isMember( "enabled" ) && s["enabled"].isBool() )
            seamEnabled = s["enabled"].asBool();
        if ( s.isMember( "maxCells" ) && s["maxCells"].isInt() )
            seamMaxCells = std::max( 16, s["maxCells"].asInt() );
        if ( s.isMember( "weights" ) && s["weights"].isObject() )
        {
            const Json::Value &w = s["weights"];
            if ( w.isMember( "radiometric" ) && w["radiometric"].isNumeric() )
                seamWeights.radiometric = w["radiometric"].asDouble();
            if ( w.isMember( "gradient" ) && w["gradient"].isNumeric() )
                seamWeights.gradient = w["gradient"].asDouble();
            if ( w.isMember( "cloud" ) && w["cloud"].isNumeric() )
                seamWeights.cloud = w["cloud"].asDouble();
            if ( w.isMember( "edgeDistance" ) && w["edgeDistance"].isNumeric() )
                seamWeights.edgeDistance = w["edgeDistance"].asDouble();
        }
    }

    ::rs::mosaic::BlendMode blendMode = ::rs::mosaic::BlendMode::Feather;
    int featherWidth = 32;
    if ( params.isMember( "blending" ) && params["blending"].isObject() )
    {
        const Json::Value &b = params["blending"];
        if ( b.isMember( "mode" ) && b["mode"].isString() )
        {
            const std::string m = b["mode"].asString();
            if ( m == "none" )
                blendMode = ::rs::mosaic::BlendMode::None;
            else if ( m == "feather" )
                blendMode = ::rs::mosaic::BlendMode::Feather;
            else
                throw RSOperatorError( ErrorCode::InvalidEnumValue,
                                       "blending.mode must be 'none' or 'feather' "
                                       "(multiband blending is a kernel-level module)" );
        }
        if ( b.isMember( "featherWidth" ) && b["featherWidth"].isInt() )
            featherWidth = std::max( 1, b["featherWidth"].asInt() );
    }

    ::rs::mosaic::QualityWeights qualityWeights;
    ::rs::mosaic::QualityOptions qualityOptions;
    if ( params.isMember( "qualityWeights" ) && params["qualityWeights"].isObject() )
    {
        const Json::Value &q = params["qualityWeights"];
        if ( q.isMember( "cloud" ) && q["cloud"].isNumeric() )
            qualityWeights.cloud = q["cloud"].asDouble();
        if ( q.isMember( "quality" ) && q["quality"].isNumeric() )
            qualityWeights.quality = q["quality"].asDouble();
        if ( q.isMember( "time" ) && q["time"].isNumeric() )
            qualityWeights.time = q["time"].asDouble();
        if ( q.isMember( "view" ) && q["view"].isNumeric() )
            qualityWeights.view = q["view"].asDouble();
    }
    if ( params.isMember( "timeScaleDays" ) && params["timeScaleDays"].isNumeric() )
        qualityOptions.timeScaleDays = params["timeScaleDays"].asDouble();
    if ( params.isMember( "viewScaleDeg" ) && params["viewScaleDeg"].isNumeric() )
        qualityOptions.viewScaleDeg = params["viewScaleDeg"].asDouble();

    bool provenance = true;
    if ( params.isMember( "provenance" ) && params["provenance"].isBool() )
        provenance = params["provenance"].asBool();

    std::vector<int> overviewFactors { 2, 4, 8 };
    if ( params.isMember( "overviews" ) && params["overviews"].isArray() )
    {
        overviewFactors.clear();
        for ( const auto &o : params["overviews"] )
        {
            if ( !o.isInt() || o.asInt() < 2 )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "overviews entries must be integers >= 2" );
            overviewFactors.push_back( o.asInt() );
        }
    }

    // ---- Input inspection ----------------------------------------------------
    const Json::Value &inputsJson = params["inputs"];
    const int inputCount = static_cast<int>( inputsJson.size() );
    std::vector<InputSpec> specs;
    specs.reserve( static_cast<size_t>( inputCount ) );
    for ( int i = 0; i < inputCount; ++i )
        specs.push_back( parseInput( inputsJson[i] ) );

    std::vector<GdalDatasetWrapper> datasets( static_cast<size_t>( inputCount ) );
    std::vector<::rs::mosaic::SceneEntry> scenes( static_cast<size_t>( inputCount ) );
    std::vector<float> nodataPerScene( static_cast<size_t>( inputCount ),
                                       std::numeric_limits<float>::quiet_NaN() );
    int minBands = std::numeric_limits<int>::max();

    for ( int i = 0; i < inputCount; ++i )
    {
        const std::string &path = specs[static_cast<size_t>( i )].path;
        if ( !fileExists( path ) )
            throw RSOperatorError( ErrorCode::FileNotFound, "Input not found: " + path );
        if ( !datasets[static_cast<size_t>( i )].open( QString::fromStdString( path ) ) )
            throw RSOperatorError( ErrorCode::GdalError, "Failed to open: " + path );
        const GdalDatasetWrapper &ds = datasets[static_cast<size_t>( i )];
        ::rs::mosaic::SceneEntry &se = scenes[static_cast<size_t>( i )];
        se.path = path;
        se.width = ds.width();
        se.height = ds.height();
        se.geoTransform = ds.geoTransform();
        se.crsWkt = ds.projection().toStdString();
        se.bandCount = ds.bandCount();
        se.priority = specs[static_cast<size_t>( i )].priority;
        bool hasNd = false;
        const double nd = ds.bandNoDataValue( 1, &hasNd );
        se.hasNodata = hasNd;
        se.nodata = hasNd ? static_cast<float>( nd ) : 0.0f;
        nodataPerScene[static_cast<size_t>( i )] =
            hasNd ? static_cast<float>( nd ) : std::numeric_limits<float>::quiet_NaN();
        minBands = std::min( minBands, se.bandCount );
        context.throwIfCancelled();
    }
    int bandCount = minBands;
    if ( bandCount < 1 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Inputs have no raster bands to mosaic" );
    if ( params.isMember( "bandCount" ) && params["bandCount"].isInt() &&
         params["bandCount"].asInt() > 0 )
    {
        const int requested = params["bandCount"].asInt();
        if ( requested > minBands )
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                "bandCount " + std::to_string( requested ) +
                    " exceeds the smallest input band count " + std::to_string( minBands ) );
        bandCount = requested;
    }

    // ---- Plan (Package A) ------------------------------------------------------
    context.reportProgress( 0.05, "Building mosaic grid plan" );
    ::rs::mosaic::MosaicPlan plan;
    {
        std::string planError;
        auto built = ::rs::mosaic::MosaicPlanner::build( scenes, {}, &planError );
        if ( !built )
            throw RSOperatorError( ErrorCode::InvalidInputData, planError );
        plan = std::move( *built );
    }
    for ( const std::string &w : plan.diagnostics.warnings )
    {
        if ( w.find( "sub-pixel" ) == std::string::npos )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "Quality mosaic inputs are not grid-compatible: " + w );
        context.logWarning( w );
    }
    const uint64_t outPixels =
        static_cast<uint64_t>( plan.width ) * static_cast<uint64_t>( plan.height );
    if ( outPixels > kMaxOutputPixels )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Mosaic output too large (" + std::to_string( plan.width ) +
                                   "x" + std::to_string( plan.height ) + ")" );

    // ---- Samplers & sidecars ----------------------------------------------------
    GdalOverlapSampler sampler( &datasets, nodataPerScene, &plan.scenes );
    std::vector<CloudMaskSidecar> cloudMasks( static_cast<size_t>( inputCount ) );
    std::vector<bool> hasCloudMask( static_cast<size_t>( inputCount ), false );
    for ( int i = 0; i < inputCount; ++i )
    {
        const InputSpec &spec = specs[static_cast<size_t>( i )];
        if ( spec.cloudMaskPath.empty() )
            continue;
        QString err;
        const ::rs::mosaic::SceneEntry &se = plan.scenes[static_cast<size_t>( i )].scene;
        const ::rs::mosaic::ScenePlacement &pl = plan.scenes[static_cast<size_t>( i )].placement;
        if ( !cloudMasks[static_cast<size_t>( i )].bind( spec.cloudMaskPath, se.geoTransform,
                                                         se.width, se.height, pl.offsetX,
                                                         pl.offsetY, &err ) )
            throw RSOperatorError( ErrorCode::InvalidInputData, err.toStdString() );
        hasCloudMask[static_cast<size_t>( i )] = true;
    }

    // ---- Balancing (Package B) ---------------------------------------------------
    context.reportProgress( 0.10, "Radiometric balancing" );
    std::vector<::rs::mosaic::SceneBalance> balances( static_cast<size_t>( inputCount ) );
    for ( int i = 0; i < inputCount; ++i )
    {
        balances[static_cast<size_t>( i )].scene = i;
        balances[static_cast<size_t>( i )].perBand.assign(
            static_cast<size_t>( bandCount ), ::rs::mosaic::BalanceGain {} );
    }
    std::vector<int> rejectedInputs;
    if ( balanceEnabled && inputCount > 1 )
    {
        std::string balanceError;
        if ( !::rs::mosaic::RadiometricBalancer::balance( plan, sampler, bandCount, balanceOptions,
                                                        &balances, &balanceError ) )
            throw RSOperatorError( ErrorCode::InvalidInputData, balanceError );
    }
    for ( int i = 0; i < inputCount; ++i )
        if ( balances[static_cast<size_t>( i )].rejected )
            rejectedInputs.push_back( i );

    // ---- Quality scoring & paint order (Package E) --------------------------------
    std::vector<::rs::mosaic::SceneQualityInput> qualityInputs( static_cast<size_t>( inputCount ) );
    for ( int i = 0; i < inputCount; ++i )
    {
        const InputSpec &spec = specs[static_cast<size_t>( i )];
        qualityInputs[static_cast<size_t>( i )].hasCloud = spec.hasCloud;
        qualityInputs[static_cast<size_t>( i )].cloudFraction = spec.cloudFraction;
        qualityInputs[static_cast<size_t>( i )].hasQuality = spec.hasQuality;
        qualityInputs[static_cast<size_t>( i )].quality = spec.quality;
        qualityInputs[static_cast<size_t>( i )].hasTime = spec.hasTime;
        qualityInputs[static_cast<size_t>( i )].timeDays = spec.timeDays;
        qualityInputs[static_cast<size_t>( i )].hasView = spec.hasView;
        qualityInputs[static_cast<size_t>( i )].viewAngleDeg = spec.viewAngleDeg;
    }
    std::vector<::rs::mosaic::QualityScore> scores( static_cast<size_t>( inputCount ) );
    for ( int i = 0; i < inputCount; ++i )
        scores[static_cast<size_t>( i )] = ::rs::mosaic::QualityScorer::score(
            qualityInputs[static_cast<size_t>( i )], qualityWeights, qualityOptions );

    // paintOrder: worst painted first, best painted last (best scene wins
    // contested pixels through seam decisions and final fills).
    std::vector<int> paintOrder =
        ::rs::mosaic::QualityScorer::compositeOrder( scores, std::vector<int>() );
    std::reverse( paintOrder.begin(), paintOrder.end() );

    // ---- Seam decisions (Package C) -------------------------------------------------
    context.reportProgress( 0.15, "Computing seamlines" );
    std::map<std::pair<int, int>, SeamEntry> seams;
    if ( method == "seamline" && seamEnabled && inputCount > 1 )
    {
        for ( const ::rs::mosaic::OverlapPair &pair : plan.overlaps )
        {
            if ( balances[static_cast<size_t>( pair.a )].rejected ||
                 balances[static_cast<size_t>( pair.b )].rejected )
                continue;
            const ::rs::mosaic::ScenePlacement &pa =
                plan.scenes[static_cast<size_t>( pair.a )].placement;
            const ::rs::mosaic::ScenePlacement &pb =
                plan.scenes[static_cast<size_t>( pair.b )].placement;
            const ::rs::mosaic::SceneEntry &ea = plan.scenes[static_cast<size_t>( pair.a )].scene;
            const ::rs::mosaic::SceneEntry &eb = plan.scenes[static_cast<size_t>( pair.b )].scene;
            const int64_t rx0 = std::max<int64_t>( pa.offsetX, pb.offsetX );
            const int64_t ry0 = std::max<int64_t>( pa.offsetY, pb.offsetY );
            const int64_t rx1 = std::min<int64_t>( pa.offsetX + ea.width, pb.offsetX + eb.width );
            const int64_t ry1 = std::min<int64_t>( pa.offsetY + ea.height, pb.offsetY + eb.height );
            const int64_t rw = rx1 - rx0;
            const int64_t rh = ry1 - ry0;
            if ( rw <= 0 || rh <= 0 )
                continue;

            ::rs::mosaic::BinnedSeamCost builder( rw, rh, seamWeights, seamMaxCells );
            const int64_t window = 512;
            for ( int64_t y = 0; y < rh; y += window )
            {
                const int64_t hh = std::min<int64_t>( window, rh - y );
                for ( int64_t x = 0; x < rw; x += window )
                {
                    const int64_t ww = std::min<int64_t>( window, rw - x );
                    std::vector<float> va, vb;
                    if ( !sampler.readGridWindow( pair.a, 0, rx0 + x, ry0 + y, ww, hh, va ) ||
                         !sampler.readGridWindow( pair.b, 0, rx0 + x, ry0 + y, ww, hh, vb ) )
                        throw RSOperatorError( ErrorCode::GdalError,
                                               "Failed to read overlap window for seamline" );
                    const auto &ga = balances[static_cast<size_t>( pair.a )].perBand[0];
                    const auto &gb = balances[static_cast<size_t>( pair.b )].perBand[0];
                    for ( size_t t = 0; t < va.size(); ++t )
                    {
                        if ( std::isfinite( va[t] ) )
                            va[t] = static_cast<float>( ga.apply( va[t] ) );
                        if ( std::isfinite( vb[t] ) )
                            vb[t] = static_cast<float>( gb.apply( vb[t] ) );
                    }
                    std::vector<float> penA, penB;
                    if ( cloudMasks[static_cast<size_t>( pair.a )].bound() &&
                         !cloudMasks[static_cast<size_t>( pair.a )].readPenalty(
                             rx0 + x, ry0 + y, ww, hh, penA ) )
                        throw RSOperatorError( ErrorCode::GdalError,
                                               "Failed to read cloud mask for seamline" );
                    if ( cloudMasks[static_cast<size_t>( pair.b )].bound() &&
                         !cloudMasks[static_cast<size_t>( pair.b )].readPenalty(
                             rx0 + x, ry0 + y, ww, hh, penB ) )
                        throw RSOperatorError( ErrorCode::GdalError,
                                               "Failed to read cloud mask for seamline" );
                    builder.addWindow( x, y, ww, hh, va, vb, penA, penB );
                }
                context.throwIfCancelled();
            }

            SeamEntry entry;
            entry.rectX = rx0;
            entry.rectY = ry0;
            entry.decision = builder.solve();
            entry.cellW = std::max<int64_t>( 1, ( rw + builder.cellsX() - 1 ) / builder.cellsX() );
            entry.cellH = std::max<int64_t>( 1, ( rh + builder.cellsY() - 1 ) / builder.cellsY() );

            // Side semantics keyed by scene index: the *smaller scene index*
            // owns the negative side of the path. decideSeam's geometric
            // convention is "negative side = left (vertical) / above
            // (horizontal)"; flip when the smaller index sits on the other
            // side.
            const bool vertical = entry.decision.orientation ==
                                  ::rs::mosaic::SeamOrientation::Vertical;
            const int lo = std::min( pair.a, pair.b );
            const bool loIsA = ( pair.a < pair.b );
            const bool aOnNegative =
                vertical ? ( pa.offsetX < pb.offsetX ||
                             ( pa.offsetX == pb.offsetX && pa.offsetY <= pb.offsetY ) )
                         : ( pa.offsetY < pb.offsetY ||
                             ( pa.offsetY == pb.offsetY && pa.offsetX <= pb.offsetX ) );
            const bool loOnNegative = loIsA ? aOnNegative : !aOnNegative;
            entry.negativeSideScene = loOnNegative ? lo : std::max( pair.a, pair.b );

            seams[{ lo, std::max( pair.a, pair.b ) }] = entry;
        }
    }

    // ---- Output dataset (tiled GTiff via a temp path, published by rename) -------
    context.reportProgress( 0.20, "Creating output dataset" );
    const QString finalPath = QString::fromStdString( outputPath );
    const QString partPath = finalPath + QStringLiteral( ".part.tif" );
    if ( QFile::exists( partPath ) )
        QFile::remove( partPath );

    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        throw RSOperatorError( ErrorCode::GdalError, "GTiff driver unavailable" );
    const int outBands = bandCount + ( provenance ? 1 : 0 );
    char **creationOptions = nullptr;
    creationOptions = CSLAddString( creationOptions, "TILED=YES" );
    creationOptions = CSLAddString( creationOptions, "BLOCKXSIZE=512" );
    creationOptions = CSLAddString( creationOptions, "BLOCKYSIZE=512" );
    creationOptions = CSLAddString( creationOptions, "COMPRESS=DEFLATE" );
    creationOptions = CSLAddString( creationOptions, "BIGTIFF=IF_SAFER" );
    GDALDatasetH outDs = GDALCreate( driver, partPath.toUtf8().constData(), plan.width,
                                     plan.height, outBands, GDT_Float32, creationOptions );
    CSLDestroy( creationOptions );
    if ( !outDs )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create mosaic output: " + partPath.toStdString() );

    // Armed until commit: any exception path removes the temp file.
    struct OutputGuard
    {
        GDALDatasetH *ds;
        QString part;
        bool armed = true;
        ~OutputGuard()
        {
            if ( !armed )
                return;
            if ( *ds )
            {
                GDALClose( *ds );
                *ds = nullptr;
            }
            QFile::remove( part );
        }
    } outputGuard { &outDs, partPath, true };

    float outNodata = std::numeric_limits<float>::quiet_NaN();
    for ( int i = 0; i < inputCount; ++i )
    {
        if ( !std::isnan( nodataPerScene[static_cast<size_t>( i )] ) )
        {
            outNodata = nodataPerScene[static_cast<size_t>( i )];
            break;
        }
    }
    for ( int b = 1; b <= outBands; ++b )
        GDALSetRasterNoDataValue( GDALGetRasterBand( outDs, b ), outNodata );

    // ---- Streaming composite --------------------------------------------------------
    context.reportProgress( 0.22, "Compositing tiles" );
    std::vector<std::uint64_t> contribution( static_cast<size_t>( inputCount ), 0 );

    std::vector<float> tileOut( static_cast<size_t>( bandCount ) * kTileSize * kTileSize );
    std::vector<float> provOut( static_cast<size_t>( kTileSize ) * kTileSize, 0.0f );
    std::vector<float> winnerWeight( static_cast<size_t>( kTileSize ) * kTileSize, 0.0f );

    const int nx = ( plan.width + kTileSize - 1 ) / kTileSize;
    const int ny = ( plan.height + kTileSize - 1 ) / kTileSize;
    const uint64_t totalTiles = static_cast<uint64_t>( nx ) * ny;
    uint64_t doneTiles = 0;

    for ( int ty = 0; ty < plan.height; ty += kTileSize )
    {
        const int th = std::min( kTileSize, plan.height - ty );
        for ( int tx = 0; tx < plan.width; tx += kTileSize )
        {
            const int tw = std::min( kTileSize, plan.width - tx );
            const size_t pixels = static_cast<size_t>( tw ) * th;
            std::fill_n( tileOut.begin(), static_cast<std::ptrdiff_t>( pixels * bandCount ),
                         outNodata );
            std::fill_n( provOut.begin(), static_cast<std::ptrdiff_t>( pixels ), 0.0f );
            std::fill_n( winnerWeight.begin(), static_cast<std::ptrdiff_t>( pixels ), 0.0f );

            for ( int sceneIdx : paintOrder )
            {
                if ( balances[static_cast<size_t>( sceneIdx )].rejected )
                    continue;
                const ::rs::mosaic::ScenePlacement &placement =
                    plan.scenes[static_cast<size_t>( sceneIdx )].placement;
                const ::rs::mosaic::SceneEntry &sceneEntry =
                    plan.scenes[static_cast<size_t>( sceneIdx )].scene;
                if ( !placement.valid )
                    continue;

                const int64_t ix0 = std::max<int64_t>( tx, placement.offsetX );
                const int64_t iy0 = std::max<int64_t>( ty, placement.offsetY );
                const int64_t ix1 = std::min<int64_t>( tx + tw, placement.offsetX + sceneEntry.width );
                const int64_t iy1 = std::min<int64_t>( ty + th, placement.offsetY + sceneEntry.height );
                if ( ix0 >= ix1 || iy0 >= iy1 )
                    continue;
                const int rw = static_cast<int>( ix1 - ix0 );
                const int rh = static_cast<int>( iy1 - iy0 );

                std::vector<std::vector<float>> bandData( static_cast<size_t>( bandCount ) );
                for ( int b = 0; b < bandCount; ++b )
                {
                    if ( !sampler.readGridWindow( sceneIdx, b, ix0, iy0, rw, rh,
                                                  bandData[static_cast<size_t>( b )] ) )
                        throw RSOperatorError( ErrorCode::GdalError,
                                               "Failed to read window from input " +
                                                   std::to_string( sceneIdx + 1 ) );
                }
                std::vector<float> cloudPen;
                const bool hasMask = cloudMasks[static_cast<size_t>( sceneIdx )].bound();
                if ( hasMask &&
                     !cloudMasks[static_cast<size_t>( sceneIdx )].readPenalty( ix0, iy0, rw, rh,
                                                                               cloudPen ) )
                    throw RSOperatorError( ErrorCode::GdalError,
                                           "Failed to read cloud mask window for input " +
                                               std::to_string( sceneIdx + 1 ) );

                const ::rs::mosaic::SceneBalance &balance = balances[static_cast<size_t>( sceneIdx )];

                for ( int r = 0; r < rh; ++r )
                {
                    for ( int c = 0; c < rw; ++c )
                    {
                        const size_t local = static_cast<size_t>( r ) * rw + c;
                        if ( hasMask && cloudPen[local] >= 1.0f )
                            continue; // fully cloudy: no contribution

                        const int64_t gx = ix0 + c;
                        const int64_t gy = iy0 + r;
                        const size_t tileIdx =
                            static_cast<size_t>( gy - ty ) * tw + static_cast<size_t>( gx - tx );

                        // Blend weight of the incoming scene at this pixel.
                        double s = 1.0;
                        if ( provOut[tileIdx] > 0.0f )
                        {
                            // Contested pixel: seam decision vs canvas scene.
                            const int canvasScene = static_cast<int>( provOut[tileIdx] ) - 1;
                            const auto it = seams.find( { std::min( canvasScene, sceneIdx ),
                                                          std::max( canvasScene, sceneIdx ) } );
                            if ( it != seams.end() )
                            {
                                const SeamEntry &seam = it->second;
                                double d = seam.signedDistance( gx, gy );
                                if ( sceneIdx == seam.negativeSideScene )
                                    d = -d; // positive d must mean "incoming side"
                                s = blendMode == ::rs::mosaic::BlendMode::Feather
                                        ? ::rs::mosaic::featherWeight( d, featherWidth )
                                        : ( d >= 0.0 ? 1.0 : 0.0 );
                            }
                            // Without a seam entry the later-painted scene
                            // wins outright (deterministic fallback).
                        }
                        else
                        {
                            s = 1.0; // unfilled pixel: full contribution
                        }

                        if ( s <= 0.0 )
                            continue;

                        // Apply & validate the incoming values first.
                        std::vector<float> applied( static_cast<size_t>( bandCount ),
                                                    std::numeric_limits<float>::quiet_NaN() );
                        bool anyValid = false;
                        for ( int b = 0; b < bandCount; ++b )
                        {
                            const float v = bandData[static_cast<size_t>( b )][local];
                            if ( std::isnan( v ) )
                                continue;
                            applied[static_cast<size_t>( b )] = static_cast<float>(
                                balance.perBand[static_cast<size_t>( b )].apply( v ) );
                            anyValid = true;
                        }
                        if ( !anyValid )
                            continue;

                        if ( provOut[tileIdx] == 0.0f )
                        {
                            for ( int b = 0; b < bandCount; ++b )
                                tileOut[static_cast<size_t>( b ) * pixels + tileIdx] =
                                    applied[static_cast<size_t>( b )];
                            provOut[tileIdx] = static_cast<float>( sceneIdx ) + 1.0f;
                            winnerWeight[tileIdx] = 1.0f;
                            ++contribution[static_cast<size_t>( sceneIdx )];
                            continue;
                        }

                        if ( s >= 1.0 )
                        {
                            for ( int b = 0; b < bandCount; ++b )
                                tileOut[static_cast<size_t>( b ) * pixels + tileIdx] =
                                    applied[static_cast<size_t>( b )];
                            provOut[tileIdx] = static_cast<float>( sceneIdx ) + 1.0f;
                            winnerWeight[tileIdx] = 1.0f;
                            ++contribution[static_cast<size_t>( sceneIdx )];
                            continue;
                        }

                        // Feather zone: blend per band with NoData fallback.
                        for ( int b = 0; b < bandCount; ++b )
                        {
                            const float canvasV =
                                tileOut[static_cast<size_t>( b ) * pixels + tileIdx];
                            tileOut[static_cast<size_t>( b ) * pixels + tileIdx] =
                                ::rs::mosaic::blendValue(
                                    canvasV, applied[static_cast<size_t>( b )], s );
                        }
                        if ( s > winnerWeight[tileIdx] )
                        {
                            provOut[tileIdx] = static_cast<float>( sceneIdx ) + 1.0f;
                            winnerWeight[tileIdx] = static_cast<float>( s );
                        }
                        ++contribution[static_cast<size_t>( sceneIdx )];
                    }
                }
                context.throwIfCancelled();
            }

            for ( int b = 1; b <= bandCount; ++b )
            {
                if ( GDALRasterIO( GDALGetRasterBand( outDs, b ), GF_Write, tx, ty, tw, th,
                                   tileOut.data() + static_cast<size_t>( b - 1 ) * pixels, tw,
                                   th, GDT_Float32, 0, 0 ) != CE_None )
                    throw RSOperatorError( ErrorCode::GdalError,
                                           "Failed to write mosaic tile at (" +
                                               std::to_string( tx ) + ", " +
                                               std::to_string( ty ) + ")" );
            }
            if ( provenance )
            {
                if ( GDALRasterIO( GDALGetRasterBand( outDs, outBands ), GF_Write, tx, ty, tw,
                                   th, provOut.data(), tw, th, GDT_Float32, 0, 0 ) != CE_None )
                    throw RSOperatorError( ErrorCode::GdalError,
                                           "Failed to write provenance tile at (" +
                                               std::to_string( tx ) + ", " +
                                               std::to_string( ty ) + ")" );
            }

            ++doneTiles;
            context.reportProgress( 0.22 + 0.68 * ( static_cast<double>( doneTiles ) / totalTiles ),
                                    "Compositing tiles (" + std::to_string( doneTiles ) + "/" +
                                        std::to_string( totalTiles ) + ")" );
            context.throwIfCancelled();
        }
    }

    // ---- Georeference, overviews, close ------------------------------------------------
    double gt[6];
    for ( int i = 0; i < 6; ++i )
        gt[i] = plan.gridTransform[static_cast<size_t>( i )];
    GDALSetGeoTransform( outDs, gt );
    GDALSetProjection( outDs, plan.crsWkt.c_str() );
    if ( provenance )
        GDALSetRasterNoDataValue( GDALGetRasterBand( outDs, outBands ), 0 );

    if ( !overviewFactors.empty() )
    {
        if ( GDALBuildOverviews( outDs, "AVERAGE", static_cast<int>( overviewFactors.size() ),
                                 overviewFactors.data(), 0, nullptr, nullptr,
                                 nullptr ) != CE_None )
            context.logWarning(
                "Overview build failed (non-fatal); the mosaic remains valid without pyramids" );
    }

    GDALClose( outDs );
    outDs = nullptr;
    outputGuard.armed = false;

    if ( QFile::exists( finalPath ) && !QFile::remove( finalPath ) )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Cannot replace existing output: " + outputPath );
    if ( !QFile::rename( partPath, finalPath ) )
    {
        QFile::remove( partPath );
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Cannot publish mosaic output to " + outputPath );
    }

    // ---- Result & report ---------------------------------------------------------------
    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["width"] = plan.width;
    result["height"] = plan.height;
    result["bandCount"] = bandCount;
    result["inputCount"] = inputCount;
    Json::Value rejectedJson( Json::arrayValue );
    for ( int i : rejectedInputs )
        rejectedJson.append( i );
    result["rejectedInputs"] = rejectedJson;
    result["seamCount"] = static_cast<Json::ArrayIndex>( seams.size() );

    if ( params.isMember( "reportOutput" ) && params["reportOutput"].isString() &&
         !params["reportOutput"].asString().empty() )
    {
        context.reportProgress( 0.95, "Writing quality report" );
        Json::Value report( Json::objectValue );
        report["schema"] = "exp-rs/quality-mosaic-report@1";
        report["output"] = outputPath;
        report["method"] = method;
        Json::Value grid( Json::objectValue );
        grid["width"] = plan.width;
        grid["height"] = plan.height;
        grid["crs"] = plan.crsWkt;
        grid["pixelSizeX"] = plan.gridTransform[1];
        grid["pixelSizeY"] = plan.gridTransform[5];
        report["grid"] = grid;
        Json::Value inputsReport( Json::arrayValue );
        for ( int i = 0; i < inputCount; ++i )
        {
            Json::Value ir( Json::objectValue );
            ir["index"] = i;
            ir["path"] = specs[static_cast<size_t>( i )].path;
            ir["score"] = scores[static_cast<size_t>( i )].score;
            ir["rejected"] = balances[static_cast<size_t>( i )].rejected;
            ir["referenceHop"] = balances[static_cast<size_t>( i )].referenceHop;
            ir["contributionPixels"] = static_cast<Json::UInt64>(
                contribution[static_cast<size_t>( i )] );
            Json::Value gains( Json::arrayValue );
            for ( int b = 0; b < bandCount; ++b )
            {
                Json::Value g( Json::objectValue );
                g["gain"] = balances[static_cast<size_t>( i )].perBand[static_cast<size_t>( b )].gain;
                g["bias"] = balances[static_cast<size_t>( i )].perBand[static_cast<size_t>( b )].bias;
                gains.append( g );
            }
            ir["balancing"] = gains;
            inputsReport.append( ir );
        }
        report["inputs"] = inputsReport;
        report["seamCount"] = static_cast<Json::ArrayIndex>( seams.size() );

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::string reportError;
        if ( !::rs::fusion::writeTextFileAtomic( params["reportOutput"].asString(),
                                               Json::writeString( builder, report ),
                                               &reportError ) )
            throw RSOperatorError( ErrorCode::FileNotWritable, reportError );
    }

    context.reportProgress( 1.0, "Quality mosaic complete" );
    return result;
}

} // namespace sicnu::operators::rs

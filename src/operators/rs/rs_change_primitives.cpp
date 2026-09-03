/***************************************************************************
 * rs_change_primitives.cpp  —  Atomic change-detection metric operators
 ***************************************************************************/
#include "rs_change_primitives.h"

#include "data/raster_grid_compat.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
#include "rs_change_streaming.h"

#include <QString>

#include <cstdint>
#include <optional>
#include <string>

namespace sicnu::operators::rs {

using namespace params;

namespace {

/// Shared validation + execution for the single-metric primitives.
Json::Value runPrimitive( ChangeMetric metric, const std::string &label,
                          const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
    {
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );
    }

    const std::string beforePath = requireString( params, "before" );
    const std::string afterPath = requireString( params, "after" );
    const std::string outputPath = requireString( params, "output" );

    if ( !fileExists( beforePath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "Before raster not found: " + beforePath );
    if ( !fileExists( afterPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "After raster not found: " + afterPath );

    const int beforeBand = getInt( params, "beforeBand", getInt( params, "band", 1 ) );
    const int afterBand = getInt( params, "afterBand", getInt( params, "band", 1 ) );

    ensureGdalInit();

    GdalDatasetWrapper beforeDs;
    if ( !beforeDs.open( QString::fromStdString( beforePath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open before raster: " + beforePath );
    GdalDatasetWrapper afterDs;
    if ( !afterDs.open( QString::fromStdString( afterPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open after raster: " + afterPath );

    const int width = beforeDs.width();
    const int height = beforeDs.height();

    // Same validation as the facade: radiometric comparability, grid
    // compatibility, dimensions, band range / band-count equality.
    const QString beforeState = SatelliteProducts::readRadiometricState( QString::fromStdString( beforePath ) );
    const QString afterState = SatelliteProducts::readRadiometricState( QString::fromStdString( afterPath ) );
    if ( !beforeState.isEmpty() && !afterState.isEmpty() && beforeState != afterState )
    {
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "Before and after rasters are in different radiometric states (" +
            beforeState.toStdString() + " vs " + afterState.toStdString() +
            "); calibrate or atmospherically correct both acquisitions to the "
            "same state before comparing them" );
    }

    const sicnu::data::GridCompatReport gridReport =
        sicnu::data::compareGrids( sicnu::processing::gridFromDataset( beforeDs ),
                                   sicnu::processing::gridFromDataset( afterDs ) );
    for ( const sicnu::data::GridCompatIssue &issue : gridReport.issues )
    {
        if ( issue.blocking )
            throw RSOperatorError( ErrorCode::InvalidInputData, issue.message.toStdString() );
        context.logWarning( issue.message.toStdString() );
    }

    if ( afterDs.width() != width || afterDs.height() != height )
    {
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Before and after rasters must have the same dimensions" );
    }

    const bool multiBand = ( metric == ChangeMetric::Cva || metric == ChangeMetric::Mad );
    if ( multiBand )
    {
        if ( beforeDs.bandCount() != afterDs.bandCount() )
        {
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   label + " requires the same band count on both rasters" );
        }
    }
    else
    {
        if ( beforeBand < 1 || beforeBand > beforeDs.bandCount() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "Before band " + std::to_string( beforeBand ) + " is out of range" );
        if ( afterBand < 1 || afterBand > afterDs.bandCount() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "After band " + std::to_string( afterBand ) + " is out of range" );
    }

    ChangeStreamingOptions opts;
    opts.beforeBand = beforeBand;
    opts.afterBand = afterBand;
    opts.makeMask = false;
    opts.outputPath = outputPath;
    opts.methodLabel = label;

    return runChangeStreaming( beforeDs, afterDs, width, height, metric, opts, context );
}

/// Shared schema: before/after/output + band selection.
Json::Value primitiveSchema( const std::string &displayName, const std::string &description )
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["before"] = makeRasterParam( "before", "Before-date raster" );
    props["after"] = makeRasterParam( "after", "After-date raster" );
    // Machine-readable data contracts consumed by preflight (same-grid +
    // radiometric comparability).
    for ( const char *port : { "before", "after" } )
    {
        props[port]["x-rs-contract"]["dataKind"] = "raster";
        props[port]["x-rs-contract"]["gridRelation"] = "same-grid";
        Json::Value states( Json::arrayValue );
        states.append( SatelliteProducts::kRadiometricStateDigitalNumber );
        states.append( SatelliteProducts::kRadiometricStateRadiance );
        states.append( SatelliteProducts::kRadiometricStateToaReflectance );
        states.append( SatelliteProducts::kRadiometricStateSurfaceReflectance );
        props[port]["x-rs-contract"]["radiometricState"] = states;
    }
    props["output"] = makeOutputParam( "output", "Output change raster", "tif" );
    props["band"] = makeIntegerParam( "band", "1-based band for both images (fallback)", 1 );
    props["beforeBand"] = makeIntegerParam( "beforeBand", "1-based band on before image (overrides band)", 0 );
    props["afterBand"] = makeIntegerParam( "afterBand", "1-based band on after image (overrides band)", 0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["method"] = makeStringParam( "method", "Applied method", "" );
    outputs["mean"] = makeNumberParam( "mean", "Mean of change magnitude", 0.0 );
    outputs["stddev"] = makeNumberParam( "stddev", "Stddev of change magnitude", 0.0 );

    Json::Value root = makeRootSchema( displayName, description, props, outputs );
    root["required"] = makeRequired( { "before", "after", "output" } );
    return root;
}

/// Input-dependent estimate: derives the tile working set from the actual band
/// count of the before raster when available (overflow-safe); falls back to the
/// static typical-input estimate otherwise. @p tilesOfBands is the per-atom
/// tile working set in "tiles of bands" (BIP input tiles plus any per-tile
/// band-major copies and scratch — e.g. 3 for the simple magnitude atoms, 5
/// for SAM/IR-MAD which scatter the BIP tile into band-major buffers).
Json::Value primitiveEstimate( const Json::Value &params, std::uint64_t tilesOfBands = 3 )
{
    if ( params.isObject() && params.isMember( "before" ) && params["before"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["before"].asString() ) )
             && probe.bandCount() > 0 )
        {
            const std::uint64_t bands = static_cast<std::uint64_t>( probe.bandCount() );
            std::optional<std::uint64_t> ram =
                sicnu::processing::checkedMulN(
                    { 256ULL, 256ULL, bands, static_cast<std::uint64_t>( sizeof( float ) ),
                      tilesOfBands } );
            if ( ram )
            {
                Json::Value est( Json::objectValue );
                est["tileWidth"] = Json::Value::UInt64( 256 );
                est["tileHeight"] = Json::Value::UInt64( 256 );
                est["estimatedRamBytes"] = Json::Value::UInt64( *ram );
                est["basis"] = "dynamic";
                return est;
            }
        }
    }
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value primitiveMetadata( const std::string &description, const std::string &facadeId )
{
    Json::Value meta( Json::objectValue );
    meta["group"] = "temporal";
    meta["displayName"] = description;
    meta["description"] = description;
    meta["tags"].append( "change-detection" );
    meta["tags"].append( "temporal" );
    meta["purpose"] = "Atomic change metric; chain with rs:threshold_raster to build a change mask.";
    meta["prerequisites"].append(
        "Before and after rasters must be co-registered and same size (grid compatibility is preflighted)." );
    meta["workflowHints"].append(
        "Apply atmospheric correction to both dates before comparison." );
    meta["workflowHints"].append(
        "Composable primitive: rs:" + facadeId + " is the multi-method facade." );
    meta["facadeOf"] = facadeId;
    return meta;
}

} // anonymous namespace

// --- rs:change_difference ---------------------------------------------------

Json::Value RsChangeDifferenceOperator::schema() const
{
    return primitiveSchema( "Change Difference", "Pixel-wise after - before change." );
}
Json::Value RsChangeDifferenceOperator::metadata() const
{
    return primitiveMetadata( "Pixel-wise difference between two rasters (after - before).", "change_detection" );
}
Json::Value RsChangeDifferenceOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    // 256x256 tile in/out buffers (2 x float tile) — O(tile), independent of raster size.
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * 256ULL * 256ULL * 4ULL );
    return est;
}
Json::Value RsChangeDifferenceOperator::estimateExecution( const Json::Value &params ) const
{
    return primitiveEstimate( params );
}

Json::Value RsChangeDifferenceOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runPrimitive( ChangeMetric::Difference, "difference", params, context );
}

// --- rs:change_normalized_difference ---------------------------------------

Json::Value RsChangeNormalizedDifferenceOperator::schema() const
{
    return primitiveSchema( "Change Normalized Difference", "(after - before) / (after + before)." );
}
Json::Value RsChangeNormalizedDifferenceOperator::metadata() const
{
    return primitiveMetadata( "Normalized difference change metric.", "change_detection" );
}
Json::Value RsChangeNormalizedDifferenceOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * 256ULL * 256ULL * 4ULL );
    return est;
}
Json::Value RsChangeNormalizedDifferenceOperator::estimateExecution( const Json::Value &params ) const
{
    return primitiveEstimate( params );
}

Json::Value RsChangeNormalizedDifferenceOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runPrimitive( ChangeMetric::NormalizedDifference, "normalized_difference", params, context );
}

// --- rs:change_ratio --------------------------------------------------------

Json::Value RsChangeRatioOperator::schema() const
{
    return primitiveSchema( "Change Ratio", "after / before (NaN where before is <= 0)." );
}
Json::Value RsChangeRatioOperator::metadata() const
{
    return primitiveMetadata( "Ratio change metric after/before.", "change_detection" );
}
Json::Value RsChangeRatioOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * 256ULL * 256ULL * 4ULL );
    return est;
}
Json::Value RsChangeRatioOperator::estimateExecution( const Json::Value &params ) const
{
    return primitiveEstimate( params );
}

Json::Value RsChangeRatioOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runPrimitive( ChangeMetric::Ratio, "ratio", params, context );
}

// --- rs:change_cva ----------------------------------------------------------

Json::Value RsChangeCvaOperator::schema() const
{
    using namespace schema;
    Json::Value root = primitiveSchema( "Change Vector Analysis",
                                        "Change Vector Analysis magnitude across all bands." );
    return root;
}
Json::Value RsChangeCvaOperator::metadata() const
{
    return primitiveMetadata( "Change Vector Analysis magnitude (all bands).", "change_detection" );
}
Json::Value RsChangeCvaOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * 256ULL * 256ULL * 4ULL );
    return est;
}
Json::Value RsChangeCvaOperator::estimateExecution( const Json::Value &params ) const
{
    return primitiveEstimate( params );
}

Json::Value RsChangeCvaOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runPrimitive( ChangeMetric::Cva, "cva", params, context );
}

// --- rs:change_mad ----------------------------------------------------------

Json::Value RsChangeMadOperator::schema() const
{
    return primitiveSchema( "Multivariate Alteration Detection",
                            "MAD change magnitude (canonical correlation analysis)." );
}
Json::Value RsChangeMadOperator::metadata() const
{
    return primitiveMetadata( "Multivariate Alteration Detection change magnitude.", "change_detection" );
}
Json::Value RsChangeMadOperator::executionEstimate() const
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    // 2 BIP input tiles + output tile + scratch + bands^2 covariance state (6-band nominal).
    constexpr long long kTilePixels = 256LL * 256;
    constexpr long long kBandCount = 6;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        3LL * kTilePixels * kBandCount * static_cast<long long>( sizeof( float ) )
        + kTilePixels * static_cast<long long>( sizeof( float ) )
        + kBandCount * kBandCount * static_cast<long long>( sizeof( double ) ) );
    return est;
}
Json::Value RsChangeMadOperator::estimateExecution( const Json::Value &params ) const
{
    return primitiveEstimate( params );
}

Json::Value RsChangeMadOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runPrimitive( ChangeMetric::Mad, "mad", params, context );
}

// --- rs:change_cva_angle ----------------------------------------------------

Json::Value RsChangeCvaAngleOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["before"] = makeRasterParam( "before", "Before-date 2-band raster" );
    props["after"] = makeRasterParam( "after", "After-date 2-band raster" );
    props["output"] = makeOutputParam( "output", "Output change angle or quadrant raster", "tif" );
    props["beforeBand1"] = makeIntegerParam( "beforeBand1", "1-based Band 1 on before image", 1 );
    props["beforeBand2"] = makeIntegerParam( "beforeBand2", "1-based Band 2 on before image", 2 );
    props["afterBand1"] = makeIntegerParam( "afterBand1", "1-based Band 1 on after image", 1 );
    props["afterBand2"] = makeIntegerParam( "afterBand2", "1-based Band 2 on after image", 2 );
    props["mode"] = makeEnumParam( "mode", "Output mode: 'angle' (radians [-pi, pi]) or 'quadrant' (1..4)",
                                   { "angle", "quadrant" }, "angle" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["method"] = makeStringParam( "method", "Applied method", "cva_angle" );
    outputs["mode"] = makeStringParam( "mode", "Angle or quadrant mode", "angle" );
    outputs["width"] = makeIntegerParam( "width", "Raster width", 0 );
    outputs["height"] = makeIntegerParam( "height", "Raster height", 0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "before", "after", "output" } );
    return root;
}

Json::Value RsChangeCvaAngleOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = "temporal";
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "change-detection" );
    meta["tags"].append( "cva" );
    meta["tags"].append( "directional" );
    meta["purpose"] = "Compute Change Vector Analysis directional angle (radians) or 4-quadrant sector classification.";
    meta["prerequisites"].append( "Before and after rasters must be co-registered with identical dimensions." );
    meta["facadeOf"] = "change_detection";
    return meta;
}

Json::Value RsChangeCvaAngleOperator::executionEstimate() const
{
    // 256x256 tile working set: 2 x 2-band BIP input tiles, 4 band-major tile
    // buffers, magnitude + angle + read-scratch tiles, one Byte quadrant tile —
    // O(tile), independent of raster size.
    constexpr long long kTilePixels = 256LL * 256;
    constexpr long long kFloatTiles = 11LL;
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        static_cast<std::uint64_t>( kFloatTiles * kTilePixels
                                    * static_cast<long long>( sizeof( float ) ) + kTilePixels ) );
    return est;
}

Json::Value RsChangeCvaAngleOperator::estimateExecution( const Json::Value & ) const
{
    // The CVA-angle working set is fixed at 2 bands regardless of the input
    // band count, so the static tile estimate is already exact.
    return executionEstimate();
}

Json::Value RsChangeCvaAngleOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Parameters must be a JSON object" );

    const std::string beforePath = requireString( params, "before" );
    const std::string afterPath = requireString( params, "after" );
    const std::string outputPath = requireString( params, "output" );
    const std::string mode = params.isMember( "mode" )
                                 ? getEnum( params, "mode", { "angle", "quadrant" }, "angle" )
                                 : "angle";

    if ( !fileExists( beforePath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "Before raster not found: " + beforePath );
    if ( !fileExists( afterPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "After raster not found: " + afterPath );

    ensureGdalInit();
    GdalDatasetWrapper beforeDs, afterDs;
    if ( !beforeDs.open( QString::fromStdString( beforePath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open before raster: " + beforePath );
    if ( !afterDs.open( QString::fromStdString( afterPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open after raster: " + afterPath );

    const int width = beforeDs.width();
    const int height = beforeDs.height();
    if ( afterDs.width() != width || afterDs.height() != height )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Rasters must have identical dimensions" );

    const int b1 = getInt( params, "beforeBand1", 1 );
    const int b2 = getInt( params, "beforeBand2", 2 );
    const int a1 = getInt( params, "afterBand1", 1 );
    const int a2 = getInt( params, "afterBand2", 2 );

    if ( b1 < 1 || b1 > beforeDs.bandCount() || b2 < 1 || b2 > beforeDs.bandCount() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Before band numbers out of range" );
    if ( a1 < 1 || a1 > afterDs.bandCount() || a2 < 1 || a2 > afterDs.bandCount() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "After band numbers out of range" );

    // Tile-streamed: the kernel runs per 256x256 tile and only the tile's
    // angle/magnitude buffers are resident (rs_change_streaming.h).
    ChangeAtomStreamingOptions atomOpts;
    atomOpts.beforeBand = b1;
    atomOpts.beforeBand2 = b2;
    atomOpts.afterBand = a1;
    atomOpts.afterBand2 = a2;
    atomOpts.mode = mode;
    atomOpts.outputPath = outputPath;

    return runCvaAngleStreaming( beforeDs, afterDs, width, height, atomOpts, context );
}

// --- rs:change_sam ----------------------------------------------------------

Json::Value RsChangeSamOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["before"] = makeRasterParam( "before", "Before-date multi-band raster" );
    props["after"] = makeRasterParam( "after", "After-date multi-band raster" );
    props["output"] = makeOutputParam( "output", "Output spectral angle change raster (radians)", "tif" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["method"] = makeStringParam( "method", "Applied method", "sam" );
    outputs["mean"] = makeNumberParam( "mean", "Mean spectral angle (rad)", 0.0 );
    outputs["stddev"] = makeNumberParam( "stddev", "Stddev of spectral angle", 0.0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "before", "after", "output" } );
    return root;
}

Json::Value RsChangeSamOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = "temporal";
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "change-detection" );
    meta["tags"].append( "sam" );
    meta["tags"].append( "spectral-angle" );
    meta["purpose"] = "Spectral Angle Mapper (SAM) change detection measuring spectral shape divergence.";
    meta["prerequisites"].append( "Before and after rasters must have equal band count and dimensions." );
    meta["facadeOf"] = "change_detection";
    return meta;
}

Json::Value RsChangeSamOperator::executionEstimate() const
{
    // 256x256 tile working set: 2 BIP input tiles + 2 per-tile band-major
    // copies (SAM needs band-major pointers) + scratch/output tile —
    // O(tile * bands), independent of raster size.
    constexpr long long kTilePixels = 256LL * 256;
    constexpr long long kBandCount = 6; // nominal multi-spectral scene
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        static_cast<std::uint64_t>( ( 4LL * kBandCount + 2LL ) * kTilePixels
                                    * static_cast<long long>( sizeof( float ) ) ) );
    return est;
}

Json::Value RsChangeSamOperator::estimateExecution( const Json::Value &params ) const
{
    // 2 BIP input tiles + 2 band-major scatter tiles + output/scratch.
    return primitiveEstimate( params, 5 );
}

Json::Value RsChangeSamOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Parameters must be a JSON object" );

    const std::string beforePath = requireString( params, "before" );
    const std::string afterPath = requireString( params, "after" );
    const std::string outputPath = requireString( params, "output" );

    if ( !fileExists( beforePath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "Before raster not found: " + beforePath );
    if ( !fileExists( afterPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "After raster not found: " + afterPath );

    ensureGdalInit();
    GdalDatasetWrapper beforeDs, afterDs;
    if ( !beforeDs.open( QString::fromStdString( beforePath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open before raster: " + beforePath );
    if ( !afterDs.open( QString::fromStdString( afterPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open after raster: " + afterPath );

    const int width = beforeDs.width();
    const int height = beforeDs.height();
    const int bandCount = beforeDs.bandCount();
    if ( afterDs.width() != width || afterDs.height() != height )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Rasters must have identical dimensions" );
    if ( afterDs.bandCount() != bandCount )
        throw RSOperatorError( ErrorCode::InvalidInputData, "SAM requires identical band counts on before and after rasters" );

    // Tile-streamed: per-tile band-major vectors feed the SAM kernel; the
    // working set is O(tilePixels * bands), never a full frame.
    ChangeAtomStreamingOptions atomOpts;
    atomOpts.outputPath = outputPath;

    return runSamStreaming( beforeDs, afterDs, width, height, atomOpts, context );
}

// --- rs:change_log_ratio ----------------------------------------------------

Json::Value RsChangeLogRatioOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["before"] = makeRasterParam( "before", "Before-date raster" );
    props["after"] = makeRasterParam( "after", "After-date raster" );
    props["output"] = makeOutputParam( "output", "Output log ratio change raster", "tif" );
    props["band"] = makeIntegerParam( "band", "1-based band for both images", 1 );
    props["beforeBand"] = makeIntegerParam( "beforeBand", "1-based band on before image", 0 );
    props["afterBand"] = makeIntegerParam( "afterBand", "1-based band on after image", 0 );
    props["epsilon"] = makeNumberParam( "epsilon", "Small positive constant to prevent ln(0)", 1e-4 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["method"] = makeStringParam( "method", "Applied method", "log_ratio" );
    outputs["mean"] = makeNumberParam( "mean", "Mean of log ratio change", 0.0 );
    outputs["stddev"] = makeNumberParam( "stddev", "Stddev of log ratio change", 0.0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "before", "after", "output" } );
    return root;
}

Json::Value RsChangeLogRatioOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = "temporal";
    meta["task"] = "change-detection";
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "change-detection" );
    meta["tags"].append( "log-ratio" );
    meta["tags"].append( "sar" );
    meta["purpose"] = "Log-Ratio change detection ln(after + eps) - ln(before + eps).";
    meta["prerequisites"].append( "Before and after rasters must be co-registered and same size." );
    meta["facadeOf"] = "change_detection";
    return meta;
}

Json::Value RsChangeLogRatioOperator::executionEstimate() const
{
    // 256x256 tile working set: before + after + read-scratch + output tiles —
    // O(tile), independent of raster size (single band per date).
    constexpr long long kTilePixels = 256LL * 256;
    constexpr long long kFloatTiles = 4LL;
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        static_cast<std::uint64_t>( kFloatTiles * kTilePixels
                                    * static_cast<long long>( sizeof( float ) ) ) );
    return est;
}

Json::Value RsChangeLogRatioOperator::estimateExecution( const Json::Value & ) const
{
    // Single-band per date: the working set is fixed at 4 tiles regardless of
    // the input band count, so the static tile estimate is already exact.
    return executionEstimate();
}

Json::Value RsChangeLogRatioOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Parameters must be a JSON object" );

    const std::string beforePath = requireString( params, "before" );
    const std::string afterPath = requireString( params, "after" );
    const std::string outputPath = requireString( params, "output" );
    const float epsilon = static_cast<float>( getDouble( params, "epsilon", 1e-4 ) );

    if ( !fileExists( beforePath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "Before raster not found: " + beforePath );
    if ( !fileExists( afterPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "After raster not found: " + afterPath );

    ensureGdalInit();
    GdalDatasetWrapper beforeDs, afterDs;
    if ( !beforeDs.open( QString::fromStdString( beforePath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open before raster: " + beforePath );
    if ( !afterDs.open( QString::fromStdString( afterPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open after raster: " + afterPath );

    const int width = beforeDs.width();
    const int height = beforeDs.height();
    if ( afterDs.width() != width || afterDs.height() != height )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Rasters must have identical dimensions" );

    const int defaultBand = getInt( params, "band", 1 );
    const int bBand = getInt( params, "beforeBand", defaultBand );
    const int aBand = getInt( params, "afterBand", defaultBand );

    if ( bBand < 1 || bBand > beforeDs.bandCount() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Before band out of range" );
    if ( aBand < 1 || aBand > afterDs.bandCount() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "After band out of range" );

    // Tile-streamed: the per-pixel log ratio runs per 256x256 tile with the
    // same masked-read semantics as the streaming ratio primitive.
    ChangeAtomStreamingOptions atomOpts;
    atomOpts.beforeBand = bBand;
    atomOpts.afterBand = aBand;
    atomOpts.epsilon = epsilon;
    atomOpts.outputPath = outputPath;

    return runLogRatioStreaming( beforeDs, afterDs, width, height, atomOpts, context );
}

// --- rs:change_irmad --------------------------------------------------------

Json::Value RsChangeIrMadOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["before"] = makeRasterParam( "before", "Before-date multi-band raster" );
    props["after"] = makeRasterParam( "after", "After-date multi-band raster" );
    props["output"] = makeOutputParam( "output", "Output IR-MAD Chi-Square change raster", "tif" );
    props["maxIterations"] = makeIntegerParam( "maxIterations", "Maximum IR-MAD reweighting iterations", 20 );
    props["convThreshold"] = makeNumberParam( "convThreshold", "Convergence threshold on max delta canonical correlation", 1e-4 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["method"] = makeStringParam( "method", "Applied method", "irmad" );
    outputs["mean"] = makeNumberParam( "mean", "Mean of Chi-Square change distance", 0.0 );
    outputs["stddev"] = makeNumberParam( "stddev", "Stddev of Chi-Square change distance", 0.0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "before", "after", "output" } );
    return root;
}

Json::Value RsChangeIrMadOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = "temporal";
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "change-detection" );
    meta["tags"].append( "irmad" );
    meta["tags"].append( "canonical-correlation" );
    meta["purpose"] = "Iteratively Reweighted Multivariate Alteration Detection (IR-MAD) with iterative Chi-Square sample weights.";
    meta["prerequisites"].append( "Before and after rasters must have equal band count and dimensions." );
    meta["facadeOf"] = "change_detection";
    return meta;
}

Json::Value RsChangeIrMadOperator::executionEstimate() const
{
    // Multi-pass streaming: per-pass 256x256 tile working set (2 BIP input
    // tiles + 2 tile copies + scratch/output, 6-band nominal) plus the
    // documented full-resolution Float64 weight frame carried between
    // reweighting iterations (8 bytes per pixel; sized exactly by
    // estimateExecution when the raster can be probed).
    constexpr long long kTilePixels = 256LL * 256;
    constexpr long long kBandCount = 6;
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        static_cast<std::uint64_t>( ( 4LL * kBandCount + 2LL ) * kTilePixels
                                    * static_cast<long long>( sizeof( float ) ) ) );
    est["note"] = "plus one Float64 weight frame (8 bytes per pixel) held "
                  "between IR-MAD reweighting iterations";
    return est;
}

Json::Value RsChangeIrMadOperator::estimateExecution( const Json::Value &params ) const
{
    // Tile passes: 2 BIP input tiles + 2 tile copies + output/scratch;
    // plus the exact weight-frame bytes when the raster can be probed.
    Json::Value est = primitiveEstimate( params, 5 );
    if ( params.isObject() && params.isMember( "before" ) && params["before"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["before"].asString() ) ) )
        {
            const std::uint64_t weightBytes =
                static_cast<std::uint64_t>( probe.width() )
                * static_cast<std::uint64_t>( probe.height() ) * static_cast<std::uint64_t>( sizeof( double ) );
            est["estimatedRamBytes"] = Json::Value::UInt64(
                est["estimatedRamBytes"].asUInt64() + weightBytes );
        }
    }
    return est;
}

Json::Value RsChangeIrMadOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Parameters must be a JSON object" );

    const std::string beforePath = requireString( params, "before" );
    const std::string afterPath = requireString( params, "after" );
    const std::string outputPath = requireString( params, "output" );
    const int maxIterations = getInt( params, "maxIterations", 20 );
    const double convThreshold = getDouble( params, "convThreshold", 1e-4 );

    if ( !fileExists( beforePath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "Before raster not found: " + beforePath );
    if ( !fileExists( afterPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "After raster not found: " + afterPath );

    ensureGdalInit();
    GdalDatasetWrapper beforeDs, afterDs;
    if ( !beforeDs.open( QString::fromStdString( beforePath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open before raster: " + beforePath );
    if ( !afterDs.open( QString::fromStdString( afterPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open after raster: " + afterPath );

    const int width = beforeDs.width();
    const int height = beforeDs.height();
    const int bandCount = beforeDs.bandCount();
    if ( afterDs.width() != width || afterDs.height() != height )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Rasters must have identical dimensions" );
    if ( afterDs.bandCount() != bandCount )
        throw RSOperatorError( ErrorCode::InvalidInputData, "IR-MAD requires identical band counts on before and after rasters" );

    // Tile-streamed multi-pass IR-MAD: every iteration's weighted-sum and
    // covariance passes, each reweighting pass and the final transform stream
    // 256x256 tiles (rs_change_streaming.h). The algorithm's only cross-pass
    // per-pixel state is the Chi-square weight vector, carried as one
    // Float64 frame (8 B/px) instead of the legacy full band stacks.
    ChangeAtomStreamingOptions atomOpts;
    atomOpts.maxIterations = maxIterations;
    atomOpts.convThreshold = convThreshold;
    atomOpts.outputPath = outputPath;

    return runIrMadStreaming( beforeDs, afterDs, width, height, atomOpts, context );
}

} // namespace sicnu::operators::rs

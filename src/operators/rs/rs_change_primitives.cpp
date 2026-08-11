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
/// static typical-input estimate otherwise.
Json::Value primitiveEstimate( const Json::Value &params )
{
    if ( params.isObject() && params.isMember( "before" ) && params["before"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["before"].asString() ) )
             && probe.bandCount() > 0 )
        {
            const std::uint64_t bands = static_cast<std::uint64_t>( probe.bandCount() );
            // 2 BIP input tiles + output tile + band scratch ≈ 3 tiles of bands.
            std::optional<std::uint64_t> ram =
                sicnu::processing::checkedMulN(
                    { 256ULL, 256ULL, bands, static_cast<std::uint64_t>( sizeof( float ) ), 3ULL } );
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
    return primitiveSchema( "Change Ratio", "after / before (NaN where before is 0)." );
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

} // namespace sicnu::operators::rs

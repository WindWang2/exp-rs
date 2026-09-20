/***************************************************************************
 * rs_spectral_detection_operators.cpp — Milestone C (matched filter + ACE),
 * Spectral Intelligence 12.0 (CEM) and Spectral Intelligence 13.0
 * (TCIMF, OSP).
 *
 * All detectors share the RX operator's streamed valid-pixel predicate.
 * Background matrix by family:
 *   MF/ACE    mean + covariance (mean-centered), three passes
 *   CEM/TCIMF correlation (raw second moment), two passes
 *   OSP       none — the undesired subspace is an input, one scoring pass
 * Only the per-pixel scoring kernel and the background statistics differ.
 ***************************************************************************/
#include "rs_spectral_detection_operators.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_anomaly.h"
#include "processing/algorithms/spectral_cem.h"
#include "processing/algorithms/spectral_detection.h"
#include "processing/algorithms/spectral_osp.h"
#include "processing/algorithms/spectral_tcimf.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
#include "rs_spectral_reference_input.h"

#include <QString>

#include <gdal.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

/// Shared driver. @a kind selects the scoring kernel
/// ("mf", "ace", "cem", "tcimf" or "osp").
Json::Value runDetector( const std::string &kind, const Json::Value &params,
                         RSOperatorContext &context )
{
    const bool isCem = ( kind == "cem" );
    const bool isTcimf = ( kind == "tcimf" );
    const bool isOsp = ( kind == "osp" );
    const bool usesCorrelation = isCem || isTcimf; // second-moment background
    const bool usesInterference = isTcimf || isOsp;
    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    if ( !fileExists( inputPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound, "Input raster not found: " + inputPath );

    // OSP has no background pass, so a background raster would be silently
    // ignored — refuse instead of letting the caller believe it was used.
    if ( isOsp && params.isMember( "background" ) && params["background"].isString() &&
         !params["background"].asString().empty() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "rs:osp_detection consumes no background statistics: the "
                               "undesired subspace comes from 'interference'; use "
                               "rs:tcimf_detection or rs:cem_detection with 'background' "
                               "for background-driven detection" );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if ( bandCount < 2 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               kind + " detection requires at least 2 bands, got " +
                                   std::to_string( bandCount ) );

    // Target spectrum via the shared seam: inline array, targetRef artifact
    // (table/library) or libraryPath — exactly one spectrum after resolution.
    std::vector<int> allBands( static_cast<size_t>( bandCount ) );
    for ( int b = 0; b < bandCount; ++b )
        allBands[static_cast<size_t>( b )] = b + 1;
    QString gridError;
    const RasterWavelengthGrid inputGrid = RasterWavelengthGrid::read( ds, allBands, &gridError );
    if ( !gridError.isEmpty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, gridError.toStdString() );
    const ResolvedSpectralReference targetResolved = resolveSpectralReference(
        params, "target", "targetRef", ds, allBands, inputGrid );
    const float *target = targetResolved.single();
    if ( !target )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "'target' must resolve to exactly one spectrum, got " +
                                   std::to_string( targetResolved.count ) );

    // Interference (undesired) signatures through the same seam. 'libraryPath'
    // stays reserved for the target: interference uses 'interference' /
    // 'interferenceRef' so a library-sourced target cannot silently double as
    // the interference matrix.
    ResolvedSpectralReference interferenceResolved;
    std::vector<std::vector<float>> interference;
    if ( usesInterference )
    {
        bool hasInterferenceSource =
            ( params.isMember( "interference" ) && params["interference"].isArray() &&
              !params["interference"].empty() ) ||
            ( params.isMember( "interferenceRef" ) && params["interferenceRef"].isString() &&
              !params["interferenceRef"].asString().empty() );
        if ( !hasInterferenceSource )
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                kind + " detection requires 'interference' (array of spectra) or "
                      "'interferenceRef' (spectral-table/library path); 'libraryPath' is "
                      "reserved for the target" );
        interferenceResolved = resolveSpectralReference(
            params, "interference", "interferenceRef", ds, allBands, inputGrid );
        if ( interferenceResolved.count < 1 )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "'interference' must resolve to at least one spectrum" );
        interference.reserve( static_cast<size_t>( interferenceResolved.count ) );
        for ( int c = 0; c < interferenceResolved.count; ++c )
        {
            const float *row = interferenceResolved.flat.data() +
                               static_cast<size_t>( c ) * interferenceResolved.width;
            interference.emplace_back( row, row + interferenceResolved.width );
        }
    }

    constexpr int kTile = 256;
    GdalMultibandBlockStream stream( ds, bandCount, kTile, kTile );
    const int totalTiles = stream.tileCount();
    const double perTile = totalTiles > 0 ? 1.0 / totalTiles : 0.0;

    // Declared-NoData predicate identical to the RX operator's.
    std::vector<float> noDataPerBand( static_cast<size_t>( bandCount ), 0.0f );
    std::vector<uint8_t> hasNoDataPerBand( static_cast<size_t>( bandCount ), 0 );
    for ( int b = 0; b < bandCount; ++b )
    {
        bool hasNoData = false;
        const double nd = ds.bandNoDataValue( b + 1, &hasNoData );
        if ( hasNoData )
        {
            hasNoDataPerBand[static_cast<size_t>( b )] = 1;
            noDataPerBand[static_cast<size_t>( b )] = static_cast<float>( nd );
        }
    }

    double loading = 0.0;
    if ( usesCorrelation )
    {
        loading = getDouble( params, "loading", 0.0 );
        if ( !std::isfinite( loading ) || loading < 0.0 )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "'loading' must be a finite value >= 0, got " +
                                       std::to_string( loading ) );
    }

    SpectralAnomaly::BackgroundStats stats;
    size_t backgroundSamples = 0;
    int tilesSeen = 0;
    std::vector<double> invCov;
    std::vector<double> correlation;
    double backgroundCondition = -1.0;
    if ( usesCorrelation )
    {
        // CEM/TCIMF background: one second-moment pass over the raw spectra.
        SpectralCem::CorrelationStats cemStats;
        if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
                context.throwIfCancelled();
                SpectralCem::accumulateCorrelation( bip, static_cast<size_t>( tile.width ) * tile.height,
                                                    bandCount, &cemStats, true, noDataPerBand.data(),
                                                    hasNoDataPerBand.data() );
                context.reportProgress( ( ++tilesSeen ) * perTile * 0.5, "Background correlation" );
                return true;
            } ) )
            throw RSOperatorError( ErrorCode::GdalError, "Failed to stream input tiles (correlation pass)" );
        if ( cemStats.count == 0 )
            throw RSOperatorError( ErrorCode::InvalidInputData, "No valid pixels found" );
        const int minSamples = SpectralCem::minSamplesRequired( bandCount, loading > 0.0 );
        if ( cemStats.count < static_cast<size_t>( minSamples ) )
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                kind + " background is under-sampled: " + std::to_string( cemStats.count ) +
                    " valid pixels for " + std::to_string( bandCount ) + " bands (minimum " +
                    std::to_string( minSamples ) +
                    "; enable 'loading' to use the regularized floor)" );
        SpectralCem::finalizeCorrelation( &cemStats );
        backgroundSamples = cemStats.count;
        correlation = std::move( cemStats.correlation );
        context.throwIfCancelled();
        backgroundCondition = SpectralAnomaly::conditionProxy( correlation, bandCount );
    }
    else if ( !isOsp )
    {
        if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
                context.throwIfCancelled();
                SpectralAnomaly::accumulateMean( bip, static_cast<size_t>( tile.width ) * tile.height,
                                                 bandCount, &stats, true, noDataPerBand.data(),
                                                 hasNoDataPerBand.data() );
                context.reportProgress( ( ++tilesSeen ) * perTile * 0.33, "Background mean" );
                return true;
            } ) )
            throw RSOperatorError( ErrorCode::GdalError, "Failed to stream input tiles (mean pass)" );
        if ( stats.count == 0 )
            throw RSOperatorError( ErrorCode::InvalidInputData, "No valid pixels found" );
        SpectralAnomaly::finalizeMean( &stats );
        context.throwIfCancelled();

        tilesSeen = 0;
        if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
                context.throwIfCancelled();
                SpectralAnomaly::accumulateCovariance( bip, static_cast<size_t>( tile.width ) * tile.height,
                                                       bandCount, &stats, true, noDataPerBand.data(),
                                                       hasNoDataPerBand.data() );
                context.reportProgress( 0.33 + ( ++tilesSeen ) * perTile * 0.33, "Background covariance" );
                return true;
            } ) )
            throw RSOperatorError( ErrorCode::GdalError, "Failed to stream input tiles (covariance pass)" );
        SpectralAnomaly::finalizeCovariance( &stats );
        context.throwIfCancelled();

        if ( !SpectralAnomaly::invertCovariance( stats.covariance, bandCount, &invCov ) )
            throw RSOperatorError( ErrorCode::ComputationError, "Background covariance is singular" );
        backgroundSamples = stats.count;
        backgroundCondition = SpectralAnomaly::conditionProxy( stats.covariance, bandCount );
    }

    std::vector<double> scratch( static_cast<size_t>( bandCount ), 0.0 );
    SpectralDetection::TargetModel model;
    SpectralCem::Filter cemFilter;
    SpectralTcimf::Filter tcimfFilter;
    SpectralOsp::Filter ospFilter;
    double interferenceCondition = -1.0;
    QString buildError;
    if ( isOsp )
    {
        if ( !SpectralOsp::buildFilter( target, bandCount, interference, &ospFilter, &buildError,
                                        &interferenceCondition ) )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "OSP filter is degenerate: " + buildError.toStdString() );
    }
    else if ( isTcimf )
    {
        if ( !SpectralTcimf::buildFilter( target, bandCount, interference, correlation, loading,
                                          &tcimfFilter, &buildError, &interferenceCondition ) )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "TCIMF filter is degenerate: " + buildError.toStdString() );
    }
    else if ( isCem )
    {
        if ( !SpectralCem::buildFilter( target, bandCount, correlation, loading, &cemFilter ) )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "Target spectrum is degenerate against the background "
                                   "correlation (non-finite values or zero constraint denominator)" );
    }
    else
    {
        if ( !SpectralDetection::buildTargetModel( target, bandCount, stats.mean, invCov, &model ) )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "Target spectrum is degenerate against the background "
                                   "(non-finite values or zero whitened norm)" );
    }

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1, GDT_Float32,
                             ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
    out.setNoDataValue( std::numeric_limits<float>::quiet_NaN() );

    std::vector<float> tileScores;
    tilesSeen = 0;
    const double scoreStart = isOsp ? 0.0 : ( usesCorrelation ? 0.5 : 0.66 );
    const double scoreSpan = isOsp ? 1.0 : ( usesCorrelation ? 0.5 : 0.34 );
    if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
            context.throwIfCancelled();
            const size_t tilePixels = static_cast<size_t>( tile.width ) * tile.height;
            tileScores.assign( tilePixels, 0.0f );
            for ( size_t p = 0; p < tilePixels; ++p )
            {
                const float *x = bip + p * bandCount;
                tileScores[p] =
                    isOsp ? SpectralOsp::ospScore( x, ospFilter, bandCount, &scratch )
                    : isTcimf ? SpectralTcimf::tcimfScore( x, tcimfFilter, bandCount, &scratch )
                    : isCem ? SpectralCem::cemScore( x, cemFilter, bandCount, &scratch )
                    : ( kind == "mf" ) ? SpectralDetection::matchedFilterScore( x, model, stats.mean, bandCount, &scratch )
                                       : SpectralDetection::aceScore( x, model, stats.mean, invCov, bandCount, &scratch );
            }
            if ( !out.writeTile( 1, tile, tileScores.data() ) )
                return false;
            context.reportProgress( scoreStart + ( ++tilesSeen ) * perTile * scoreSpan, "Scoring" );
            return true;
        } ) )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/score tiles" );
    }

    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["detector"] = kind;
    result["bandCount"] = bandCount;
    result["width"] = width;
    result["height"] = height;
    if ( !isOsp )
    {
        result["backgroundSamples"] = static_cast<Json::UInt64>( backgroundSamples );
        if ( backgroundCondition >= 0.0 )
            result["backgroundCondition"] = backgroundCondition;
    }
    if ( usesCorrelation )
        result["loading"] = loading;
    if ( usesInterference )
    {
        result["interferenceCount"] = static_cast<Json::UInt64>( interference.size() );
        if ( interferenceCondition >= 0.0 )
            result["interferenceCondition"] = interferenceCondition;
        result["interferenceSource"] = interferenceResolved.sourceDescription.toStdString();
        if ( interferenceResolved.resampled )
            result["interferenceResampled"] = true;
        if ( !interferenceResolved.license.isEmpty() )
            result["interferenceLicense"] = interferenceResolved.license.toStdString();
    }
    result["targetSource"] = targetResolved.sourceDescription.toStdString();
    if ( targetResolved.resampled )
        result["targetResampled"] = true;
    if ( !targetResolved.license.isEmpty() )
        result["targetLicense"] = targetResolved.license.toStdString();
    context.reportProgress( 1.0, kind + " detection complete" );
    return result;
}

Json::Value detectorSchema( const std::string &displayName, const std::string &description,
                            bool withLoading = false, bool withInterference = false )
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Input multi-band raster" );
    props["output"] = makeOutputParam( "output", "Single-band detection score raster", "tif" );
    const Json::Value referenceProps = referenceInputSchemaProps(
        "target", "targetRef", "Target spectrum (one value per input band, same order)" );
    for ( const auto &key : referenceProps.getMemberNames() )
        props[key] = referenceProps[key];
    if ( withLoading )
    {
        Json::Value loading( Json::objectValue );
        loading["type"] = "number";
        loading["minimum"] = 0.0;
        loading["default"] = 0.0;
        loading["description"] =
            "Scaled diagonal loading alpha in R + alpha*(tr(R)/B)*I; enables the "
            "reduced min-sample floor (B+1 instead of 2B+2).";
        props["loading"] = loading;
    }
    if ( withInterference )
    {
        const Json::Value interferenceProps = referenceInputSchemaProps(
            "interference", "interferenceRef",
            "Undesired signatures (array of spectra, one value per input band each; the "
            "first element disambiguates a single flat spectrum)" );
        for ( const auto &key : interferenceProps.getMemberNames() )
        {
            // 'libraryPath'/'libraryMaterials' stay reserved for the target;
            // only the inline and artifact keys are offered for interference.
            if ( key == "libraryPath" || key == "libraryMaterials" )
                continue;
            props[key] = interferenceProps[key];
        }
    }

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );

    Json::Value root = makeRootSchema( displayName, description, props, outputs );
    root["required"] = makeRequired( { "input", "output" } );
    return root;
}

Json::Value detectorEstimate()
{
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    const double tileBytes = 256.0 * 256.0 * 30.0 * sizeof( float );
    const double stateBytes = 30.0 * 30.0 * sizeof( double );
    est["estimatedRamBytes"] = static_cast<Json::UInt64>( tileBytes + stateBytes );
    return est;
}

} // namespace

Json::Value RsMatchedFilterOperator::schema() const
{
    return detectorSchema( displayName(), description() );
}
Json::Value RsMatchedFilterOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "spectral" );
    meta["tags"].append( "detection" );
    meta["tags"].append( "target" );
    meta["task"] = "target-detection";
    meta["notes"] = "Three-pass streaming detection (background mean, covariance, "
                    "score) over the same valid-pixel predicate as rs:rx_anomaly; "
                    "bit-exact grade. Signed scores: threshold downstream.";
    meta["gpu"] = false;
    meta["purpose"] = "Detect pixels spectrally similar to a supplied target spectrum.";
    meta["prerequisites"].append( "'target' must have one finite value per input band." );
    meta["workflowHints"].append( "Chain rs:threshold_raster to binarize scores; combine with rs:rx_anomaly when no target is known." );
    meta["limitations"].append( "Background statistics come from the input scene itself; a separate background raster is a future extension." );
    return meta;
}
Json::Value RsMatchedFilterOperator::executionEstimate() const
{
    return detectorEstimate();
}
Json::Value RsMatchedFilterOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runDetector( "mf", params, context );
}

Json::Value RsAceOperator::schema() const
{
    return detectorSchema( displayName(), description() );
}
Json::Value RsAceOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "spectral" );
    meta["tags"].append( "detection" );
    meta["tags"].append( "target" );
    meta["task"] = "target-detection";
    meta["notes"] = "Three-pass streaming detection sharing the matched filter's "
                    "background pipeline; scores in [0,1] (1 = whitened spectrum "
                    "parallel to the target); bit-exact grade.";
    meta["gpu"] = false;
    meta["purpose"] = "Target detection with a normalized, scale-free score." ;
    meta["prerequisites"].append( "'target' must have one finite value per input band." );
    meta["workflowHints"].append( "ACE is brightness-invariant; prefer rs:matched_filter when absolute contrast matters." );
    meta["limitations"].append( "Pixels with degenerate whitened norm (no variance along any direction) score NaN." );
    return meta;
}
Json::Value RsAceOperator::executionEstimate() const
{
    return detectorEstimate();
}
Json::Value RsAceOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runDetector( "ace", params, context );
}

Json::Value RsCemOperator::schema() const
{
    return detectorSchema( displayName(), description(), true );
}
Json::Value RsCemOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "spectral" );
    meta["tags"].append( "detection" );
    meta["tags"].append( "target" );
    meta["task"] = "target-detection";
    meta["notes"] = "Two-pass streaming CEM (background correlation, score) over the "
                    "shared valid-pixel predicate; scores are signed with the target "
                    "scoring exactly 1 (distortionless constraint); bit-exact grade.";
    meta["gpu"] = false;
    meta["purpose"] = "Target detection tolerant of multiplicative brightness scaling; "
                      "complements the signed matched filter and the squared ACE.";
    meta["prerequisites"].append( "'target' must have one finite value per input band." );
    meta["prerequisites"].append( "At least 2*B+2 valid background pixels (B+1 when "
                                  "'loading' > 0) — under-sampled scenes are refused." );
    meta["workflowHints"].append( "Chain rs:threshold_raster to binarize scores; prefer "
                                  "rs:ace for a bounded [0,1] score." );
    meta["limitations"].append( "Background statistics come from the input scene itself; "
                                "a separate background raster is a future extension." );
    return meta;
}
Json::Value RsCemOperator::executionEstimate() const
{
    return detectorEstimate();
}
Json::Value RsCemOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runDetector( "cem", params, context );
}

Json::Value RsTcimfOperator::schema() const
{
    return detectorSchema( displayName(), description(), true, true );
}
Json::Value RsTcimfOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "spectral" );
    meta["tags"].append( "detection" );
    meta["tags"].append( "target" );
    meta["task"] = "target-detection";
    meta["notes"] = "Two-pass streaming TCIMF (background correlation, score) over the "
                    "shared valid-pixel predicate: the CEM filter with exact null "
                    "constraints on the interference signatures (target scores 1, "
                    "interference scores 0); bit-exact grade.";
    meta["gpu"] = false;
    meta["purpose"] = "Target detection with known undesired signatures suppressed: "
                      "the constrained companion of CEM.";
    meta["prerequisites"].append( "'target' must have one finite value per input band." );
    meta["prerequisites"].append( "At least 2*B+2 valid background pixels (B+1 when "
                                  "'loading' > 0) — under-sampled scenes are refused." );
    meta["prerequisites"].append( "'interference' (or 'interferenceRef') must resolve to "
                                  "at least one finite, non-zero spectrum per input band." );
    meta["workflowHints"].append( "Feed rs:endmember_extraction or library materials of "
                                  "the undesired classes as interference; with no "
                                  "interference the filter is exactly CEM." );
    meta["limitations"].append( "Background statistics come from the input scene itself; "
                                "a separate background raster is a future extension." );
    meta["limitations"].append( "Interference spectra must be linearly independent under "
                                "the background metric; a target inside the interference "
                                "span is refused." );
    return meta;
}
Json::Value RsTcimfOperator::executionEstimate() const
{
    return detectorEstimate();
}
Json::Value RsTcimfOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runDetector( "tcimf", params, context );
}

Json::Value RsOspOperator::schema() const
{
    return detectorSchema( displayName(), description(), false, true );
}
Json::Value RsOspOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "spectral" );
    meta["tags"].append( "detection" );
    meta["tags"].append( "target" );
    meta["task"] = "target-detection";
    meta["notes"] = "Single-pass streaming OSP: orthogonal projection of the target "
                    "onto the complement of the undesired signature subspace; every "
                    "interference signature scores exactly 0; bit-exact grade.";
    meta["gpu"] = false;
    meta["purpose"] = "Target detection when the undesired subspace is known: unlike "
                      "the covariance-based detectors, OSP needs no background pass.";
    meta["prerequisites"].append( "'target' must have one finite value per input band." );
    meta["prerequisites"].append( "'interference' (or 'interferenceRef') must resolve to "
                                  "at least one finite, non-zero spectrum per input band, "
                                  "linearly independent of the others." );
    meta["workflowHints"].append( "Scores are signed and scale with the target "
                                  "magnitude (OSP is not brightness-invariant); chain "
                                  "rs:threshold_raster to binarize." );
    meta["limitations"].append( "A target that lies (numerically) inside the undesired "
                                "subspace is refused — no filter can suppress the "
                                "interference and keep the target at the same time." );
    return meta;
}
Json::Value RsOspOperator::executionEstimate() const
{
    return detectorEstimate();
}
Json::Value RsOspOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    return runDetector( "osp", params, context );
}

} // namespace sicnu::operators::rs

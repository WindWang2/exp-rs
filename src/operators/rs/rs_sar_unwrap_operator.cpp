/***************************************************************************
 * rs_sar_unwrap_operator.cpp — reference quality-guided phase unwrapping
 * (Advanced SAR / PolSAR / InSAR 10.0, package C; DECISIONS D-001).
 *
 * Loads the (halo-less) phase and optional quality planes behind a fixed
 * budget gate, runs the deterministic reference unwrapper, and writes the
 * unwrapped phase as Float32 with NaN at unreached/invalid pixels.
 ***************************************************************************/
#include "rs_sar_unwrap_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_complex.h"
#include "processing/algorithms/sar/sar_insar.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_unwrap_provider.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QCoreApplication>
#include <QString>

#include <gdal.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr uint64_t kPlaneBudgetBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL; // 2 GiB

} // anonymous namespace

Json::Value RsSarUnwrapOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Complex (CFloat32) interferogram raster" );
    props["output"] = makeOutputParam( "output", "Output unwrapped phase raster path", "tif" );
    props["band"] = makeNumberParam( "band", "1-based complex interferogram band", 1.0 );
    props["qualityInput"] = makeRasterParam( "qualityInput",
                                             "Optional coherence raster guiding the unwrap" );
    props["qualityBand"] = makeNumberParam( "qualityBand", "1-based quality band", 1.0 );
    props["provider"] = makeStringParam( "provider",
                                         "Unwrap provider: \"builtin\" (the deterministic "
                                         "reference), or the name of an external tool — any "
                                         "unregistered name is refused, never approximated",
                                         "builtin" );
    props["providerBin"] = makeStringParam( "providerBin",
                                            "External provider binary path (highest "
                                            "discovery priority; then "
                                            "SICNU_SAR_UNWRAP_<PROVIDER>_BIN, then PATH)" );
    props["providerArgs"] = makeStringParam(
        "providerArgs",
        "JSON array of the external provider's command-line template tokens; the "
        "placeholders {input} {output} {width} {height} are substituted (required for "
        "external providers; the tool must write a raw Float32 plane of exactly "
        "width*height samples to {output})" );
    props["providerTimeoutSec"] =
        makeNumberParam( "providerTimeoutSec", "External provider process timeout "
                                               "(seconds; the process is killed beyond it)",
                         600.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output unwrapped phase raster path" );
    outputs["unwrappedPixels"] = makeStringParam( "unwrappedPixels", "Unwrapped pixel count", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output" } );
    return root;
}

Json::Value RsSarUnwrapOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "insar" );
    meta["task"] = "sar-insar";
    meta["notes"] = "Reference implementation: deterministic quality-guided flood fill. "
                    "Not residue-aware, not a global optimum; external tools slot in "
                    "through the provider seam (typed refusal when unregistered).";
    meta["gpu"] = false;
    meta["purpose"] = "Produce an absolute-phase field from a filtered wrapped "
                      "interferogram for displacement conversion.";
    meta["prerequisites"].append( "Complex interferogram (ideally filtered via "
                                  "rs:sar_phase_filter); optional coherence raster for "
                                  "quality guidance." );
    meta["limitations"].append( "Single-scale plane unwrapping: 2 GiB plane budget "
                                "(MEMORY_BUDGET_EXCEEDED beyond; use a smaller AOI)." );
    meta["limitations"].append( "Dense residue fields yield wrong 2π branches — no "
                                "branch-cut/MCF global optimization is claimed." );
    return meta;
}

Json::Value RsSarUnwrapOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( kPlaneBudgetBytes );
    return est;
}

Json::Value RsSarUnwrapOperator::estimateExecution( const Json::Value &params ) const {
    (void)params;
    return executionEstimate();
}

Json::Value RsSarUnwrapOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const int band = getInt( params, "band", 1 );
    const std::string qualityPath = getString( params, "qualityInput", std::string() );
    const int qualityBand = getInt( params, "qualityBand", 1 );
    const std::string provider = getString( params, "provider", std::string( "builtin" ) );
    if ( provider != "builtin" )
    {
        // External provider path (Advanced InSAR 11.0, package D; D-004):
        // the wrapped plane is staged for the process adapter; the built-in
        // is never silently substituted and every failure is typed.
        const std::string providerBin = getString( params, "providerBin", std::string() );
        const double timeoutSec = getDouble( params, "providerTimeoutSec", 600.0 );
        if ( !params.isMember( "providerArgs" ) || !params["providerArgs"].isArray()
             || params["providerArgs"].empty() )
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                "UNWRAP_PROVIDER_FAILED: external provider '" + provider
                    + "' needs providerArgs (a JSON array of command-line template "
                      "tokens with {input}/{output}/{width}/{height})" );

        ensureGdalInit();
        GdalDatasetWrapper extDs;
        if ( !extDs.open( QString::fromStdString( inputPath ) ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to open input raster: " + inputPath );
        if ( extDs.width() <= 0 || extDs.height() <= 0 )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "Input raster is empty: " + inputPath );
        QString complexError;
        if ( !sicnu::sar::validateComplexBands( extDs, { band }, &complexError ) )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "COMPLEX_BANDS_REQUIRED: "
                                       + complexError.toStdString() );
        const int extWidth = extDs.width();
        const int extHeight = extDs.height();
        const uint64_t extWh = static_cast<uint64_t>( extWidth ) * extHeight;
        // Wrapped plane (8 B/px) + provider staging planes (2 × 4 B/px) +
        // the adapter float copy (4 B/px) + the result double plane
        // (8 B/px) handed back from the adapter.
        if ( 28ULL * extWh > kPlaneBudgetBytes )
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "MEMORY_BUDGET_EXCEEDED: the wrapped plane plus provider staging "
                "exceeds the 2 GiB budget for " + std::to_string( extWidth ) + "x"
                    + std::to_string( extHeight ) + " — use a smaller AOI" );

        std::vector<double> wrappedExt( static_cast<size_t>( extWh ),
                                        std::numeric_limits<double>::quiet_NaN() );
        {
            sicnu::sar::ComplexBandTileStream stream( extDs, { band }, 256, 256, 0 );
            std::vector<std::complex<float>> buf(
                static_cast<size_t>( stream.bandCount() ) * 256 * 256 );
            for ( int i = 0; i < stream.tileCount(); ++i )
            {
                context.throwIfCancelled();
                const sicnu::sar::ComplexTile &tile = stream.tile( i );
                if ( !stream.readTile( i, buf.data() ) )
                    throw RSOperatorError( ErrorCode::GdalError,
                                           "Failed to read interferogram tile" );
                for ( int y = 0; y < tile.height; ++y )
                    for ( int x = 0; x < tile.width; ++x )
                        wrappedExt[static_cast<size_t>( tile.yOffset + y ) * extWidth
                                   + tile.xOffset + x] =
                            sicnu::sar::interferogramPhase(
                                buf[static_cast<size_t>( y ) * tile.bufferWidth + x],
                                { 1.0f, 0.0f } );
            }
        }

        sicnu::sar::UnwrapProviderRequest request;
        request.providerName = QString::fromStdString( provider );
        request.binPath = QString::fromStdString( providerBin );
        for ( const Json::Value &token : params["providerArgs"] )
        {
            if ( !token.isString() )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "providerArgs tokens must be strings" );
            request.argsTemplate.emplace_back(
                QString::fromStdString( token.asString() ) );
        }
        request.timeoutMs = static_cast<int>( timeoutSec * 1000.0 );
        request.workDir = QString::fromStdString( context.workDir() );
        request.cancelQuery = [ &context ] { return context.isCancelled(); };
        request.wrapped = wrappedExt.data();
        request.w = extWidth;
        request.h = extHeight;

        sicnu::sar::UnwrapProviderResult providerResult;
        QString providerError;
        const sicnu::sar::UnwrapProviderStatus status = sicnu::sar::runExternalUnwrapProvider(
            request, &providerResult, &providerError );
        switch ( status )
        {
            case sicnu::sar::UnwrapProviderStatus::Ok:
                break;
            case sicnu::sar::UnwrapProviderStatus::Cancelled:
                throw RSOperatorError( ErrorCode::Cancelled, providerError.toStdString() );
            case sicnu::sar::UnwrapProviderStatus::Unavailable:
            case sicnu::sar::UnwrapProviderStatus::Failed:
            case sicnu::sar::UnwrapProviderStatus::Timeout:
            case sicnu::sar::UnwrapProviderStatus::InvalidOutput:
                throw RSOperatorError( ErrorCode::ComputationError,
                                       providerError.toStdString() );
        }

        GdalStreamingOutput extOut( QString::fromStdString( outputPath ), extWidth,
                                    extHeight, 1, GDT_Float32, extDs.geoTransform(),
                                    extDs.projection() );
        if ( !extOut.isOpen() )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Failed to create output raster: " + outputPath );
        extOut.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
        extOut.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "unwrapped_phase" );
        extOut.setMetadataItem( "SICNU_SAR_INSAR_UNWRAP_PROVIDER", provider.c_str() );
        extOut.setBandNoDataValue( 1, std::numeric_limits<double>::quiet_NaN() );
        std::vector<float> extFloat( static_cast<size_t>( extWh ) );
        for ( size_t i = 0; i < extWh; ++i )
            extFloat[i] = static_cast<float>( providerResult.unwrapped[i] );
        if ( !extOut.writeTile(
                 1, GdalBlockStream::Tile{ 0, 0, extWidth, extHeight, 0, extWidth,
                                           extHeight, 0, 1, extWidth, extHeight },
                 extFloat.data() )
             || !extOut.closeWithError() )
        {
            extOut.abandon();
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to write the provider unwrapped phase" );
        }

        Json::Value extJson;
        extJson["output"] = outputPath;
        extJson["provider"] = provider;
        extJson["commandLine"] = providerResult.commandLine.toStdString();
        extJson["unwrappedPixels"] = Json::Value::Int64( providerResult.validCount );
        extJson["totalPixels"] = Json::Value::Int64( static_cast<long long>( extWh ) );
        context.reportProgress( 1.0, "External unwrap complete" );
        return extJson;
    }

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    if ( ds.width() <= 0 || ds.height() <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Input raster is empty: " + inputPath );

    QString error;
    if ( !sicnu::sar::validateComplexBands( ds, { band }, &error ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "COMPLEX_BANDS_REQUIRED: " + error.toStdString() );

    const int width = ds.width();
    const int height = ds.height();
    // Full plane inventory (review F2): wrapped (8) + unwrapped (8) +
    // visited (1) + write-back float copy (4) bytes per pixel, plus the
    // optional quality plane (8) — and the flood-fill priority queue adds
    // up to 4 entries/pixel (~96 B/px) transiently, counted at half weight.
    const uint64_t wh = static_cast<uint64_t>( width ) * height;
    const uint64_t planeBytes =
        21ULL * wh + ( qualityPath.empty() ? 0ULL : 8ULL * wh ) + 48ULL * wh;
    if ( planeBytes > kPlaneBudgetBytes )
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "MEMORY_BUDGET_EXCEEDED: the reference unwrapper is plane-based and "
            "refuses rasters beyond its 2 GiB budget (needs ~"
                + std::to_string( planeBytes ) + " bytes for " + std::to_string( width )
                + "x" + std::to_string( height )
                + "). Use a smaller AOI or an external unwrap provider." );

    // Phase plane from the complex interferogram (halo-less tile walk).
    std::vector<double> wrapped( static_cast<size_t>( width ) * height,
                                 std::numeric_limits<double>::quiet_NaN() );
    {
        sicnu::sar::ComplexBandTileStream stream( ds, { band }, 256, 256, 0 );
        std::vector<std::complex<float>> buf(
            static_cast<size_t>( stream.bandCount() ) * 256 * 256 );
        for ( int i = 0; i < stream.tileCount(); ++i )
        {
            context.throwIfCancelled();
            const sicnu::sar::ComplexTile &tile = stream.tile( i );
            if ( !stream.readTile( i, buf.data() ) )
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read interferogram tile" );
            for ( int y = 0; y < tile.height; ++y )
                for ( int x = 0; x < tile.width; ++x )
                {
                    const std::complex<float> z =
                        buf[static_cast<size_t>( y ) * tile.bufferWidth + x];
                    wrapped[static_cast<size_t>( tile.yOffset + y ) * width + tile.xOffset + x] =
                        sicnu::sar::interferogramPhase( z, { 1.0f, 0.0f } );
                }
        }
    }

    // Optional quality plane.
    std::vector<double> quality;
    if ( !qualityPath.empty() )
    {
        GdalDatasetWrapper qds;
        if ( !qds.open( QString::fromStdString( qualityPath ) ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to open quality raster: " + qualityPath );
        if ( qds.width() != width || qds.height() != height )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "DIMENSION_MISMATCH: quality raster must match the "
                                   "interferogram grid" );
        if ( qualityBand < 1 || qualityBand > qds.bandCount() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "qualityBand out of range" );
        quality.resize( static_cast<size_t>( width ) * height );
        std::vector<float> qbuf( static_cast<size_t>( width ) * height );
        if ( !qds.readBandMasked( qualityBand, qbuf.data(), width, height ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to read the quality plane" );
        for ( size_t i = 0; i < qbuf.size(); ++i )
            quality[i] = qbuf[i];
    }

    sicnu::sar::UnwrapResult result;
    if ( !sicnu::sar::qualityGuidedUnwrap(
             wrapped.data(), quality.empty() ? nullptr : quality.data(), width, height,
             &result, [ &context ] { context.throwIfCancelled(); } ) )
        throw RSOperatorError( ErrorCode::ComputationError,
                               "Unwrapping failed (non-finite phase plane)" );
    // Honest refusal over a silent all-NaN product: finite phase existed but
    // nothing could be seeded (typically an all-NoData quality plane).
    {
        long finitePhase = 0;
        for ( size_t i = 0; i < wrapped.size(); ++i )
            if ( std::isfinite( wrapped[i] ) )
                ++finitePhase;
        if ( finitePhase > 0 && result.unwrappedCount == 0 )
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "nothing was unwrapped: " + std::to_string( finitePhase )
                    + " finite phase pixels but no component could be seeded (check the "
                      "quality plane — every sample may be NoData)" );
    }

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1,
                             GDT_Float32, ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    out.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
    out.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "unwrapped_phase" );
    out.setMetadataItem( "SICNU_SAR_INSAR_UNWRAP_PROVIDER", provider.c_str() );
    out.setBandNoDataValue( 1, std::numeric_limits<double>::quiet_NaN() );

    context.reportProgress( 0.95, "Writing unwrapped phase" );
    // writeTile writes floats — convert the double plane.
    std::vector<float> floatPlane( static_cast<size_t>( width ) * height );
    for ( size_t i = 0; i < floatPlane.size(); ++i )
        floatPlane[i] = static_cast<float>( result.unwrapped[i] );
    if ( !out.writeTile( 1,
                         GdalBlockStream::Tile{ 0, 0, width, height, 0, width, height, 0, 1,
                                                width, height },
                         floatPlane.data() )
         || !out.closeWithError() )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to write the unwrapped phase" );
    }

    Json::Value json;
    json["output"] = outputPath;
    json["provider"] = provider;
    json["unwrappedPixels"] = Json::Value::Int64( result.unwrappedCount );
    // Use 64-bit math: on LLP64 (Windows) long is 32-bit and width*height
    // overflows past 2^31 pixels (#1228 / #1186).
    json["totalPixels"] = Json::Value::Int64( static_cast<std::int64_t>( width )
                                             * static_cast<std::int64_t>( height ) );
    context.reportProgress( 1.0, "Unwrapping complete" );
    return json;
}

} // namespace sicnu::operators::rs

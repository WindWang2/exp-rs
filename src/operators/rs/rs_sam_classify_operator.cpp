/***************************************************************************
 * rs_sam_classify_operator.cpp  —  Spectral Angle Mapper classification
 ***************************************************************************/
#include "rs_sam_classify_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_classification.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <cmath>
#include <memory>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

// Parse the `refs` parameter (array of equal-length float arrays) into a flat
// row-major buffer. Throws RSOperatorError on malformed input.
std::vector<float> parseReferenceSpectra( const Json::Value &refs, int bandCount )
{
    if ( !refs.isArray() || refs.empty() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                              "'refs' must be a non-empty array of reference spectra" );

    std::vector<float> flat;
    flat.reserve( static_cast<size_t>( refs.size() ) * static_cast<size_t>( bandCount ) );
    int idx = 0;
    for ( const auto &entry : refs )
    {
        if ( !entry.isArray() || static_cast<int>( entry.size() ) != bandCount )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                  "Reference spectrum " + std::to_string( idx ) +
                                  " must be an array of " + std::to_string( bandCount ) +
                                  " numbers" );
        for ( Json::ArrayIndex b = 0; b < entry.size(); ++b )
        {
            if ( !entry[b].isNumeric() )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                      "Reference spectrum " + std::to_string( idx ) +
                                      " contains a non-numeric value" );
            flat.push_back( static_cast<float>( entry[b].asDouble() ) );
        }
        ++idx;
    }
    return flat;
}

} // anonymous namespace

Json::Value RsSamClassifyOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Multi-band raster to classify" );
    props["output"] = makeOutputParam( "output", "Classified raster (class id per pixel)", "tif" );
    // refs: array of arrays of numbers — schema describes the outer array.
    Json::Value refsParam( Json::objectValue );
    refsParam["type"] = "array";
    refsParam["description"] = "Reference spectra: array of arrays of band-count floats";
    refsParam["items"]["type"] = "array";
    refsParam["items"]["items"]["type"] = "number";
    props["refs"] = refsParam;
    props["bands"] = makeIntegerParam( "bands", "1-based band subset (reserved; default all)", 0 );
    props["metric"] = makeEnumParam( "metric", "Spectral matching metric",
                                     { "sam", "sid" }, "sam" );
    props["angleOut"] = makeOutputParam( "angleOut", "Optional per-pixel minimum-angle/divergence raster", "tif" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Classified raster path" );
    outputs["bands"] = makeIntegerParam( "bands", "Number of bands used", 0 );
    outputs["classes"] = makeIntegerParam( "classes", "Number of reference classes", 0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output", "refs" } );
    return root;
}

Json::Value RsSamClassifyOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "classification" );
    meta["tags"].append( "hyperspectral" );
    meta["tags"].append( "spectral-angle" );
    meta["purpose"] = "Label each pixel to the reference spectrum with the smallest "
                      "spectral distance (SAM angle or Spectral Information Divergence).";
    meta["prerequisites"].append( "Reference spectra must use the same band order and units as the input raster." );
    meta["workflowHints"].append( "SAM is illumination-invariant and well-suited to hyperspectral mapping; "
                                  "SID additionally captures spectral brightness differences." );
    meta["limitations"].append( "SID requires non-negative reflectance-like spectra (a zero or negative "
                                "band invalidates the pair)." );
    return meta;
}

Json::Value RsSamClassifyOperator::executionEstimate() const
{
    // Streaming: one 256x256 BIP tile window + per-tile labels/angles/output
    // buffers. The tile window scales with the raster's band count
    // (tilePixels*bands*sizeof(float) + output buffers), so the estimate is
    // formula-based, not a fixed constant — see RxAnomalyOperator for the same
    // rationale. Nominal 30 bands → ~7.9 MiB tile; RSS watermark backstops.
    constexpr double kTileW = 256, kTileH = 256;
    constexpr double kNominalBands = 30;
    const double tileBytes = kTileW * kTileH * kNominalBands * sizeof( float );
    const double outputBuffers = kTileW * kTileH * 2 * sizeof( float ); // labels + angles
    Json::Value est( Json::objectValue );
    est["tileWidth"] = static_cast<Json::Int64>( kTileW );
    est["tileHeight"] = static_cast<Json::Int64>( kTileH );
    est["estimatedRamBytes"] =
        static_cast<Json::UInt64>( tileBytes + outputBuffers ); // ~9.2 MiB @ 30 bands
    return est;
}

Json::Value RsSamClassifyOperator::run( const Json::Value &params, RSOperatorContext &context ) {
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    if ( !fileExists( inputPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath );

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if ( bandCount < 1 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                              "Input raster has no bands" );

    const std::vector<int> bands = parseBands( params, bandCount );
    const int nBands = static_cast<int>( bands.size() );

    std::vector<float> refs = parseReferenceSpectra( params["refs"], nBands );
    const int refCount = static_cast<int>( refs.size() / static_cast<size_t>( nBands ) );

    const std::string metric = getEnum( params, "metric", { "sam", "sid" }, "sam" );

    context.logInfo( std::string( metric == "sid" ? "SID" : "SAM" ) + " classify: " +
                     std::to_string( width ) + "x" +
                     std::to_string( height ) + ", " + std::to_string( nBands ) +
                     " bands, " + std::to_string( refCount ) + " classes" );

    // Resolve the input nodata sentinel: prefer the raster-declared band-1
    // nodata, falling back to the project convention (-9999) when none is set.
    bool hasNodata = false;
    double srcNodata = ds.bandNoDataValue( bands[0], &hasNodata );
    const float nodata = hasNodata ? static_cast<float>( srcNodata ) : -9999.0f;

    // Single-pass streaming (perf goal §2c): stream the selected bands tile-by-
    // tile as a BIP window, classify per-tile (the kernel is per-pixel), and
    // stream labels + optional angles out. The whole raster is never resident.
    constexpr int kTile = 256;
    GdalMultibandBlockStream stream( ds, bands, kTile, kTile );
    const int totalTiles = stream.tileCount();
    const double perTileProgress = totalTiles > 0 ? 1.0 / totalTiles : 0.0;

    // Output nodata is fixed (-9999) — distinct from the input-resolved nodata
    // (labels are class ids in [0, refCount) and never collide with -9999).
    constexpr float outputNodata = -9999.0f;
    GdalStreamingOutput labelOut( QString::fromStdString( outputPath ), width, height, 1,
                                  GDT_Float32, ds.geoTransform(), ds.projection() );
    if ( !labelOut.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                              "Failed to create classified raster: " + outputPath );

    const std::string anglePath = getString( params, "angleOut", "" );
    std::unique_ptr<GdalStreamingOutput> angleOut;
    if ( !anglePath.empty() )
    {
        angleOut = std::make_unique<GdalStreamingOutput>(
            QString::fromStdString( anglePath ), width, height, 1, GDT_Float32,
            ds.geoTransform(), ds.projection() );
        if ( !angleOut->isOpen() )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                  "Failed to create angle raster: " + anglePath );
    }

    int tilesSeen = 0;
    std::vector<int> tileLabels;
    std::vector<float> tileAngles;
    std::vector<float> tileLabelBand;
    std::vector<float> tileAngleBand;
    if ( !stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
            // Per-tile cancellation: a single-pass scan over a large raster must
            // stay responsive to user cancel, not just between passes.
            context.throwIfCancelled();
            const size_t tilePixels = static_cast<size_t>( tile.width ) * tile.height;
            tileLabels.assign( tilePixels, 0 );
            tileAngles.assign( tilePixels, 0.0f );
            const bool ok = ( metric == "sid" )
                ? SpectralClassification::sidClassify( bip, tilePixels, nBands,
                                                       refs.data(), refCount,
                                                       tileLabels.data(), tileAngles.data(), nodata )
                : SpectralClassification::samClassify( bip, tilePixels, nBands,
                                                       refs.data(), refCount,
                                                       tileLabels.data(), tileAngles.data(), nodata );
            if ( !ok )
                return false;
            tileLabelBand.assign( tilePixels, outputNodata );
            for ( size_t p = 0; p < tilePixels; ++p )
                if ( tileLabels[p] >= 0 )
                    tileLabelBand[p] = static_cast<float>( tileLabels[p] );
            if ( !labelOut.writeTile( 1, tile, tileLabelBand.data() ) )
                return false;
            if ( angleOut )
            {
                tileAngleBand.assign( tilePixels, outputNodata );
                for ( size_t p = 0; p < tilePixels; ++p )
                    if ( std::isfinite( tileAngles[p] ) )
                        tileAngleBand[p] = tileAngles[p];
                if ( !angleOut->writeTile( 1, tile, tileAngleBand.data() ) )
                    return false;
            }
            context.reportProgress( ( ++tilesSeen ) * perTileProgress, "Classifying" );
            return true;
        } ) )
        throw RSOperatorError( ErrorCode::ComputationError,
                              "Spectral classification kernel failed" );

    labelOut.close();
    if ( angleOut )
        angleOut->close();

    ds.close();
    context.reportProgress( 1.0, "SAM classification complete" );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["bands"] = nBands;
    result["classes"] = refCount;
    result["metric"] = metric;
    return result;
}

} // namespace sicnu::operators::rs

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

#include <QString>

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
    props["angleOut"] = makeOutputParam( "angleOut", "Optional per-pixel minimum-angle raster", "tif" );

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
    meta["purpose"] = "Label each pixel to the reference spectrum with the smallest angular distance.";
    meta["prerequisites"].append( "Reference spectra must use the same band order and units as the input raster." );
    meta["workflowHints"].append( "SAM is illumination-invariant and well-suited to hyperspectral mapping." );
    return meta;
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

    context.logInfo( "SAM classify: " + std::to_string( width ) + "x" +
                     std::to_string( height ) + ", " + std::to_string( nBands ) +
                     " bands, " + std::to_string( refCount ) + " classes" );
    context.reportProgress( 0.15, "Reading bands" );

    // Resolve the input nodata sentinel: prefer the raster-declared band-1
    // nodata, falling back to the project convention (-9999) when none is set.
    // Using the source nodata lets the kernel correctly detect nodata pixels
    // for rasters whose nodata is 0/NaN/-32768 rather than -9999.
    bool hasNodata = false;
    double srcNodata = ds.bandNoDataValue( bands[0], &hasNodata );
    const float nodata = hasNodata ? static_cast<float>( srcNodata ) : -9999.0f;

    // Read selected bands into a pixel-major buffer (pixel-major = contiguous
    // per pixel so the kernel can index [p*bands + b]).
    const size_t pixelCount = static_cast<size_t>( width ) * height;
    std::vector<float> pixels( pixelCount * static_cast<size_t>( nBands ), 0.0f );
    for ( int bi = 0; bi < nBands; ++bi )
    {
        std::vector<float> bandData( pixelCount );
        if ( !ds.readBandData( bands[bi], bandData.data(), width, height ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string( bands[bi] ) );
        for ( size_t p = 0; p < pixelCount; ++p )
            pixels[p * static_cast<size_t>( nBands ) + bi] = bandData[p];
    }

    context.reportProgress( 0.45, "Classifying" );

    std::vector<int> labels( pixelCount );
    std::vector<float> angles( pixelCount, 0.0f );
    if ( !SpectralClassification::samClassify( pixels.data(), pixelCount, nBands,
                                               refs.data(), refCount,
                                               labels.data(), angles.data(), nodata ) )
        throw RSOperatorError( ErrorCode::ComputationError,
                              "SAM classification kernel failed" );

    context.throwIfCancelled();
    context.reportProgress( 0.75, "Writing output" );

    // Output nodata is fixed (-9999) — distinct from the input-resolved nodata
    // because the label values are class ids in [0, refCount) and never
    // collide with -9999.
    constexpr float outputNodata = -9999.0f;
    std::vector<float> labelBand( pixelCount );
    for ( size_t p = 0; p < pixelCount; ++p )
        labelBand[p] = ( labels[p] < 0 ) ? outputNodata : static_cast<float>( labels[p] );

    QString errorMessage;
    if ( !writeGdalOutput( QString::fromStdString( outputPath ), width, height, { labelBand },
                           ds.geoTransform(), ds.projection(), &errorMessage ) )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                              "Failed to write classified raster: " + errorMessage.toStdString() );

    // Optional angle raster.
    const std::string anglePath = getString( params, "angleOut", "" );
    if ( !anglePath.empty() )
    {
        std::vector<float> angleBand( pixelCount );
        for ( size_t p = 0; p < pixelCount; ++p )
        {
            if ( !std::isfinite( angles[p] ) )
                angleBand[p] = outputNodata;
            else
                angleBand[p] = angles[p];
        }
        if ( !writeGdalOutput( QString::fromStdString( anglePath ), width, height, { angleBand },
                               ds.geoTransform(), ds.projection(), &errorMessage ) )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                  "Failed to write angle raster: " + errorMessage.toStdString() );
    }

    ds.close();
    context.reportProgress( 1.0, "SAM classification complete" );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["bands"] = nBands;
    result["classes"] = refCount;
    return result;
}

} // namespace sicnu::operators::rs

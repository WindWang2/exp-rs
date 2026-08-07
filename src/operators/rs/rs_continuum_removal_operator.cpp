/***************************************************************************
 * rs_continuum_removal_operator.cpp  —  Continuum-removed reflectance
 ***************************************************************************/
#include "rs_continuum_removal_operator.h"

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

Json::Value RsContinuumRemovalOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Multi-band reflectance raster" );
    props["output"] = makeOutputParam( "output", "Continuum-removed raster", "tif" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["bands"] = makeIntegerParam( "bands", "Number of bands processed", 0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output" } );
    return root;
}

Json::Value RsContinuumRemovalOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "hyperspectral" );
    meta["tags"].append( "spectral" );
    meta["tags"].append( "continuum" );
    meta["purpose"] = "Highlight absorption features by normalizing spectra to the convex hull.";
    meta["prerequisites"].append( "Input should be reflectance (0..1); DN values give meaningless ratios." );
    meta["workflowHints"].append( "Pair with band-depth or SAM for absorption-feature mapping." );
    return meta;
}

Json::Value RsContinuumRemovalOperator::run( const Json::Value &params, RSOperatorContext &context ) {
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

    context.logInfo( "Continuum removal: " + std::to_string( width ) + "x" +
                     std::to_string( height ) + ", " + std::to_string( bandCount ) + " bands" );
    context.reportProgress( 0.15, "Reading bands" );

    const size_t pixelCount = static_cast<size_t>( width ) * height;

    // Resolve the input nodata sentinel: prefer the raster-declared band-1
    // nodata, falling back to -9999 when none is set. Continuum-removal output
    // values are ratios in (0, 1], so the same sentinel is reused for output
    // nodata (degenerate pixels) without collision.
    bool hasNodata = false;
    double srcNodata = ds.bandNoDataValue( 1, &hasNodata );
    const float nodata = hasNodata ? static_cast<float>( srcNodata ) : -9999.0f;

    // Read every band into a band-major buffer, then process pixel-by-pixel.
    std::vector<std::vector<float>> bandsIn( bandCount );
    for ( int b = 0; b < bandCount; ++b )
    {
        bandsIn[b].resize( pixelCount );
        if ( !ds.readBandData( b + 1, bandsIn[b].data(), width, height ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string( b + 1 ) );
    }

    context.reportProgress( 0.45, "Applying continuum removal" );

    std::vector<std::vector<float>> bandsOut( bandCount );
    for ( int b = 0; b < bandCount; ++b )
        bandsOut[b].resize( pixelCount );

    std::vector<float> spectrum( bandCount );
    std::vector<float> removed( bandCount );
    for ( size_t p = 0; p < pixelCount; ++p )
    {
        for ( int b = 0; b < bandCount; ++b )
            spectrum[b] = bandsIn[b][p];

        if ( SpectralClassification::continuumRemoval( spectrum.data(), removed.data(),
                                                        bandCount, nodata ) )
        {
            for ( int b = 0; b < bandCount; ++b )
                bandsOut[b][p] = removed[b];
        }
        else
        {
            // Whole-pixel nodata / degenerate — fill band stack with nodata.
            for ( int b = 0; b < bandCount; ++b )
                bandsOut[b][p] = nodata;
        }
    }

    context.throwIfCancelled();
    context.reportProgress( 0.75, "Writing output" );

    QString errorMessage;
    if ( !writeGdalOutput( QString::fromStdString( outputPath ), width, height, bandsOut,
                           ds.geoTransform(), ds.projection(), &errorMessage ) )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + errorMessage.toStdString() );

    ds.close();
    context.reportProgress( 1.0, "Continuum removal complete" );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["bands"] = bandCount;
    return result;
}

} // namespace sicnu::operators::rs

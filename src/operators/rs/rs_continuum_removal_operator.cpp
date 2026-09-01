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

#include <gdal.h>

#include <QString>

#include <algorithm>
#include <cstring>
#include <numeric>
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

Json::Value RsContinuumRemovalOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    // The real tile working set is 2 x (tile^2 x bands x 4 B) (tileBip +
    // tileOut): a fixed 64 MiB under-declared hyperspectral runs by up to 4x
    // at 400 bands, so admission admitted them below their true footprint
    // (#647). Scale with the 224-band imaging-spectrometer reference, same
    // convention as rs:spectral_resample.
    est["estimatedRamBytes"] = Json::Value::UInt64( 2ULL * 256ULL * 256ULL * 224ULL * 4ULL );
    return est;
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

    const size_t pixelCount = static_cast<size_t>( width ) * height;

    // Resolve the input nodata sentinel: prefer the raster-declared band-1
    // nodata, falling back to NaN (matching only NaN pixels) when none is set (#473).
    // Continuum-removal output values are ratios in (0, 1].
    bool hasNodata = false;
    double srcNodata = ds.bandNoDataValue( 1, &hasNodata );
    const float nodata = hasNodata ? static_cast<float>( srcNodata ) : std::numeric_limits<float>::quiet_NaN();

    // Wavelengths from band WAVELENGTH metadata (fallback to indices when absent)
    std::vector<float> wavelengths( bandCount, 0.0f );
    bool hasWavelengths = true;
    for ( int b = 1; b <= bandCount; ++b )
    {
        QString wlStr = ds.bandMetadataItem( b, "WAVELENGTH" );
        bool ok = false;
        double v = wlStr.isEmpty() ? 0.0 : wlStr.toDouble( &ok );
        if ( ok && v > 0.0 ) {
            wavelengths[b - 1] = static_cast<float>( v );
        } else {
            // All-or-none (#632): mixing nm-scale wavelengths with band
            // indices lands the missing bands at x = 1..N on a 2000 nm axis,
            // badly distorting the convex hull. Fall back to pure indices.
            hasWavelengths = false;
            break;
        }
    }
    if ( !hasWavelengths )
        for ( int b = 1; b <= bandCount; ++b )
            wavelengths[b - 1] = static_cast<float>( b );

    // The continuum hull walks bands left→right and assumes the wavelength
    // axis ascends with band index (#700). Band order may legitimately differ
    // from wavelength order (positions follow the requested band list), so
    // sort the axis and track the permutation applied to the spectra.
    std::vector<int> wlPerm;  // empty = identity permutation
    std::vector<float> sortedWl;
    if ( hasWavelengths )
    {
        // Duplicate check first (review P2): an axis that is ascending WITH
        // duplicates previously skipped the guard entirely.
        for ( int b = 1; b < bandCount; ++b )
        {
            if ( wavelengths[b] == wavelengths[b - 1] )
                throw RSOperatorError( ErrorCode::InvalidInputData,
                                      "Continuum removal: duplicate WAVELENGTH metadata across bands — "
                                      "the continuum hull is ill-defined; fix the per-band WAVELENGTH tags" );
        }
        bool ascending = true;
        for ( int b = 1; b < bandCount; ++b )
        {
            if ( wavelengths[b] < wavelengths[b - 1] )
            {
                ascending = false;
                break;
            }
        }
        if ( !ascending )
        {
            wlPerm.resize( bandCount );
            std::iota( wlPerm.begin(), wlPerm.end(), 0 );
            std::stable_sort( wlPerm.begin(), wlPerm.end(),
                              [&wavelengths]( int a, int b ) { return wavelengths[a] < wavelengths[b]; } );
            sortedWl.resize( bandCount );
            for ( int b = 0; b < bandCount; ++b )
                sortedWl[b] = wavelengths[wlPerm[b]];
            context.logWarning( "Continuum removal: WAVELENGTH axis is not ascending in band order — "
                                "spectra re-ordered by wavelength for the continuum hull" );
        }
    }
    const float *wlPtr = hasWavelengths
        ? ( wlPerm.empty() ? wavelengths.data() : sortedWl.data() )
        : nullptr;

    // Stream over 256x256 BIP tiles
    constexpr int kTile = 256;
    const size_t maxTilePixels = static_cast<size_t>( kTile ) * kTile;
    const size_t B = static_cast<size_t>( bandCount );
    std::vector<float> tileBip( maxTilePixels * B, 0.0f );
    std::vector<float> tileOut( maxTilePixels * B, 0.0f );
    std::vector<float> bandScratch( maxTilePixels );
    std::vector<float> bandTile( maxTilePixels );

    QString outErr;
    GDALDatasetH outDs = createOutputTiff( QString::fromStdString( outputPath ), width, height,
                                          bandCount, static_cast<int>( GDT_Float32 ),
                                          ds.geoTransform(), ds.projection(), &outErr );
    if ( !outDs )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                              "Failed to create output raster: " + outErr.toStdString() );

    if ( hasNodata )
    {
        for ( int b = 1; b <= bandCount; ++b )
        {
            GDALRasterBandH hOutBand = GDALGetRasterBand( outDs, b );
            if ( hOutBand )
                GDALSetRasterNoDataValue( hOutBand, nodata );
        }
    }

    std::vector<float> spectrum( bandCount );
    std::vector<float> removed( bandCount );

    for ( int y = 0; y < height; y += kTile )
    {
        const int h = std::min( kTile, height - y );
        for ( int x = 0; x < width; x += kTile )
        {
            const int w = std::min( kTile, width - x );
            const size_t n = static_cast<size_t>( w ) * h;
            context.throwIfCancelled();
            context.reportProgress( 0.1 + 0.8 * ( static_cast<double>( y ) * width + x ) / pixelCount,
                                   "Applying continuum removal" );

            // Read the tile's source bands into BIP layout.
            for ( int b = 0; b < bandCount; ++b )
            {
                if ( !ds.readBandWindow( b + 1, x, y, w, h, bandScratch.data() ) )
                {
                    GDALClose( outDs );
                    throw RSOperatorError( ErrorCode::GdalError,
                                          "Failed to read input tile at (" +
                                              std::to_string( x ) + ", " + std::to_string( y ) + ")" );
                }
                for ( size_t p = 0; p < n; ++p )
                    tileBip[p * B + static_cast<size_t>( b )] = bandScratch[p];
            }

            // Apply continuum removal to each pixel's spectrum.
            for ( size_t p = 0; p < n; ++p )
            {
                const float *specIn = tileBip.data() + p * B;
                float *specOut = tileOut.data() + p * B;
                if ( wlPerm.empty() )
                {
                    std::memcpy( spectrum.data(), specIn, sizeof( float ) * bandCount );
                }
                else
                {
                    // Re-order onto the ascending wavelength axis before the hull.
                    for ( int b = 0; b < bandCount; ++b )
                        spectrum[b] = specIn[wlPerm[b]];
                }

                if ( SpectralClassification::continuumRemoval( spectrum.data(), wlPtr, removed.data(),
                                                                bandCount, nodata ) )
                {
                    if ( wlPerm.empty() )
                    {
                        std::memcpy( specOut, removed.data(), sizeof( float ) * bandCount );
                    }
                    else
                    {
                        // Invert the permutation so output bands keep the
                        // input's band order.
                        for ( int b = 0; b < bandCount; ++b )
                            specOut[wlPerm[b]] = removed[b];
                    }
                }
                else
                {
                    for ( int b = 0; b < bandCount; ++b )
                        specOut[b] = nodata;
                }
            }

            // Write each target band's tile.
            for ( int b = 0; b < bandCount; ++b )
            {
                for ( size_t p = 0; p < n; ++p )
                    bandTile[p] = tileOut[p * B + static_cast<size_t>( b )];
                GDALRasterBandH outBand = GDALGetRasterBand( outDs, b + 1 );
                if ( GDALRasterIO( outBand, GF_Write, x, y, w, h, bandTile.data(),
                                 w, h, GDT_Float32, 0, 0 ) != CE_None )
                {
                    GDALClose( outDs );
                    throw RSOperatorError( ErrorCode::FileNotWritable,
                                          "Failed to write tile at (" +
                                              std::to_string( x ) + ", " + std::to_string( y ) + ")" );
                }
            }
        }
    }

    GDALClose( outDs );
    ds.close();
    context.reportProgress( 1.0, "Continuum removal complete" );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["bands"] = bandCount;
    return result;
}

} // namespace sicnu::operators::rs

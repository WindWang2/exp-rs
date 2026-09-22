// src/processing/providers/qgis_algorithms/algorithms/raster/raster_merge_bands.cpp
#include "raster_merge_bands.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterblock.h>
#include <qgsrasterfilewriter.h>
#include <qgsrasterpipe.h>
#include <qgsrasterprojector.h>
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsgdalutils.h>
#include <qgsogrutils.h>
#include <gdal.h>
#include <QScopeGuard>

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/staged_raster_output.h"

void RasterMergeBandsAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterMultipleLayers( QStringLiteral( "INPUT_LAYERS" ), QObject::tr( "Input layers" ),
        Qgis::ProcessingSourceType::Raster ) );
    addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Merged raster" ) ) );
}

QVariantMap RasterMergeBandsAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QList<QgsMapLayer *> layers = parameterAsLayerList( parameters, QStringLiteral( "INPUT_LAYERS" ), context );
    if ( layers.isEmpty() )
        throw QgsProcessingException( QObject::tr( "No input layers provided" ) );

    // Filter to valid raster layers
    QList<QgsRasterLayer *> rasterLayers;
    for ( QgsMapLayer *layer : layers )
    {
        QgsRasterLayer *rl = qobject_cast<QgsRasterLayer *>( layer );
        if ( rl && rl->dataProvider() )
            rasterLayers.append( rl );
    }

    if ( rasterLayers.isEmpty() )
        throw QgsProcessingException( QObject::tr( "No valid raster layers found" ) );

    const QString destTarget = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

    feedback->setProgressText( QObject::tr( "Merging raster bands..." ) );

    // Use the first layer as reference for extent, CRS and dimensions
    QgsRasterLayer *refLayer = rasterLayers.first();
    QgsRectangle extent = refLayer->extent();
    QgsCoordinateReferenceSystem crs = refLayer->crs();
    int nCols = refLayer->width();
    int nRows = refLayer->height();

    for ( QgsRasterLayer *rl : rasterLayers )
    {
        if ( rl->width() != nCols || rl->height() != nRows )
        {
            throw QgsProcessingException(
                QObject::tr( "Raster layer %1 dimensions (%2x%3) do not match reference (%4x%5)" )
                    .arg( rl->name() ).arg( rl->width() ).arg( rl->height() ).arg( nCols ).arg( nRows ) );
        }
    }

    // Calculate total band count across all input layers
    int totalBands = 0;
    for ( QgsRasterLayer *rl : rasterLayers )
        totalBands += rl->bandCount();

    if ( totalBands == 0 )
        throw QgsProcessingException( QObject::tr( "Input layers have no bands" ) );

    // Read all bands from all layers into memory blocks, then write merged output
    std::vector<std::unique_ptr<QgsRasterBlock>> blocks;
    int currentBand = 0;

    for ( QgsRasterLayer *rl : rasterLayers )
    {
        if ( feedback->isCanceled() )
            return QVariantMap();

        QgsRasterDataProvider *provider = rl->dataProvider();
        for ( int band = 1; band <= provider->bandCount(); ++band )
        {
            if ( feedback->isCanceled() )
                return QVariantMap();

            std::unique_ptr<QgsRasterBlock> block( provider->block( band, extent, nCols, nRows ) );
            if ( !block || !block->isValid() )
                throw QgsProcessingException( QObject::tr( "Could not read band %1 from %2" ).arg( band ).arg( rl->name() ) );

            blocks.push_back( std::move( block ) );
            currentBand++;
            feedback->setProgress( 50.0 * currentBand / totalBands );
        }
    }

    if ( feedback->isCanceled() || static_cast<int>( blocks.size() ) < totalBands )
        return QVariantMap();

    // Write multi-band output using GDAL
    GDALDriverH hDriver = GDALGetDriverByName( "GTiff" );
    if ( !hDriver )
        throw QgsProcessingException( QObject::tr( "GTiff driver not available" ) );

    // Output type contract: the output band type is the GDALDataTypeUnion of all
    // input band types — the narrowest GDAL type that preserves every input band.
    // Same-type inputs therefore keep their type; mixed inputs promote to the
    // wider type (e.g. Byte+Float32 -> Float32) instead of silently truncating
    // wider bands to the first band's type.
    GDALDataType gdalType = GDT_Unknown;
    for ( const std::unique_ptr<QgsRasterBlock> &block : blocks )
    {
        const GDALDataType blockType = QgsGdalUtils::gdalDataTypeFromQgisDataType( block->dataType() );
        if ( blockType == GDT_Unknown )
            throw QgsProcessingException( QObject::tr( "Unsupported band data type %1" ).arg( qgsEnumValueToKey( block->dataType() ) ) );
        gdalType = ( gdalType == GDT_Unknown ) ? blockType : GDALDataTypeUnion( gdalType, blockType );
    }

    // Stage beside the destination, publish atomically on success (#617).
    auto staged = sicnu::processing::makeStagedRasterOutput( destTarget );
    if ( !staged )
        throw QgsProcessingException( QObject::tr( "Could not allocate a staging path next to %1" ).arg( destTarget ) );
    const QString dest = staged->stagedPath();
    const QByteArray destUtf8 = dest.toUtf8();
    // dataset_unique_ptr closes the dataset on every exit path (including
    // non-QgsProcessingException).
    gdal::dataset_unique_ptr hOutDs( GDALCreate( hDriver, destUtf8.constData(), nCols, nRows, totalBands, gdalType, nullptr ) );
    if ( !hOutDs )
        throw QgsProcessingException( QObject::tr( "Could not create output file %1" ).arg( dest ) );

    // Deletes the (possibly partial) output file on every early exit —
    // cancellation returns and any exception alike — so failures are not
    // mistaken for results. Dismissed once the dataset is fully written.
    auto deleteOutputOnFailure = qScopeGuard( [&hOutDs, &hDriver, &dest]()
    {
        gdal::fast_delete_and_close( hOutDs, hDriver, dest );
    } );

    double geoTransform[6] = { extent.xMinimum(), extent.width() / nCols, 0,
                               extent.yMaximum(), 0, -extent.height() / nRows };
    if ( GDALSetGeoTransform( hOutDs.get(), geoTransform ) != CE_None )
        throw QgsProcessingException( QObject::tr( "Could not set geotransform on %1" ).arg( dest ) );
    if ( crs.isValid() )
    {
        QByteArray wkt = crs.toWkt( Qgis::CrsWktVariant::Wkt1Gdal ).toUtf8();
        if ( GDALSetProjection( hOutDs.get(), wkt.constData() ) != CE_None )
            throw QgsProcessingException( QObject::tr( "Could not set projection on %1" ).arg( dest ) );
    }

    for ( int b = 0; b < totalBands; ++b )
    {
        if ( feedback->isCanceled() )
            return QVariantMap();
        GDALRasterBandH hBand = GDALGetRasterBand( hOutDs.get(), b + 1 );
        if ( !hBand )
            throw QgsProcessingException( QObject::tr( "Could not open output band %1" ).arg( b + 1 ) );
        if ( blocks[b]->hasNoDataValue() )
        {
            GDALSetRasterNoDataValue( hBand, blocks[b]->noDataValue() );
        }
        const void *data = blocks[b]->bits();
        // eBufType declares the type of the user buffer — each block's own type,
        // not the output type. GDAL converts buffer -> output band type itself.
        const GDALDataType bufType = QgsGdalUtils::gdalDataTypeFromQgisDataType( blocks[b]->dataType() );
        CPLErr cplErr = GDALRasterIO( hBand, GF_Write, 0, 0, nCols, nRows,
                                     const_cast<void *>( data ), nCols, nRows,
                                     bufType, 0, 0 );
        if ( cplErr != CE_None )
            throw QgsProcessingException( QObject::tr( "Error writing band %1" ).arg( b + 1 ) );
        feedback->setProgress( 50.0 + 50.0 * ( b + 1 ) / totalBands );
    }
    // GDALClose is where the GTiff driver reports deferred write failures
    // (disk full, quota): the per-band GF_Write checks only covered the
    // driver's cache. A failed final flush must fail the run and remove the
    // partial output — deleteOutputOnFailure is still armed on this path.
    QString closeError;
    if ( !closeDatasetFailClosed( hOutDs.release(), &closeError ) )
    {
        throw QgsProcessingException( closeError.isEmpty()
                                          ? QObject::tr( "Failed to flush merged output %1" ).arg( destTarget )
                                          : closeError );
    }
    deleteOutputOnFailure.dismiss();
    if ( !staged->publish( nullptr ) )
        throw QgsProcessingException( QObject::tr( "Failed to publish merged output %1" ).arg( destTarget ) );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = destTarget;
    return results;
}

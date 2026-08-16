// src/processing/providers/qgis_algorithms/algorithms/remote_sensing/spectral_index_algorithm.cpp
#include "spectral_index_algorithm.h"

#include "../../../../algorithms/spectral_indices.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterblock.h>
#include <qgsrasterfilewriter.h>
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>

#include <vector>

void SpectralIndexAlgorithm::initAlgorithm( const QVariantMap & )
{
    QStringList indexOptions;
    indexOptions << QStringLiteral( "NDVI" )
                 << QStringLiteral( "EVI" )
                 << QStringLiteral( "SAVI" )
                 << QStringLiteral( "NDWI" )
                 << QStringLiteral( "NDBI" )
                 << QStringLiteral( "MNDWI" );

    addParameter( new QgsProcessingParameterEnum(
        QStringLiteral( "INDEX" ), QObject::tr( "Spectral index" ), indexOptions, false, 0 ) );
    addParameter( new QgsProcessingParameterRasterLayer(
        QStringLiteral( "NIR_BAND" ), QObject::tr( "Near-infrared (NIR) band" ),
        QVariant(), true ) );
    addParameter( new QgsProcessingParameterRasterLayer(
        QStringLiteral( "RED_BAND" ), QObject::tr( "Red band" ),
        QVariant(), true ) );
    addParameter( new QgsProcessingParameterRasterLayer(
        QStringLiteral( "GREEN_BAND" ), QObject::tr( "Green band" ),
        QVariant(), true ) );
    addParameter( new QgsProcessingParameterRasterLayer(
        QStringLiteral( "BLUE_BAND" ), QObject::tr( "Blue band" ),
        QVariant(), true ) );
    addParameter( new QgsProcessingParameterRasterLayer(
        QStringLiteral( "SWIR_BAND" ), QObject::tr( "SWIR band" ),
        QVariant(), true ) );
    addParameter( new QgsProcessingParameterRasterDestination(
        QStringLiteral( "OUTPUT" ), QObject::tr( "Output spectral index" ) ) );
}

static std::vector<float> readBandData( QgsRasterDataProvider *provider, int band,
    const QgsRectangle &extent, int nCols, int nRows )
{
    std::unique_ptr<QgsRasterBlock> block( provider->block( band, extent, nCols, nRows ) );
    if ( !block || !block->isValid() )
        return {};

    size_t totalPixels = static_cast<size_t>( nCols ) * static_cast<size_t>( nRows );
    std::vector<float> data( totalPixels );
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        if ( block->isNoData( i ) )
            data[i] = std::numeric_limits<float>::quiet_NaN();
        else
            data[i] = static_cast<float>( block->value( i ) );
    }
    return data;
}

QVariantMap SpectralIndexAlgorithm::processAlgorithm( const QVariantMap &parameters,
    QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    int indexType = parameterAsEnum( parameters, QStringLiteral( "INDEX" ), context );

    QgsRasterLayer *nirLayer = parameterAsRasterLayer( parameters, QStringLiteral( "NIR_BAND" ), context );
    QgsRasterLayer *redLayer = parameterAsRasterLayer( parameters, QStringLiteral( "RED_BAND" ), context );
    QgsRasterLayer *greenLayer = parameterAsRasterLayer( parameters, QStringLiteral( "GREEN_BAND" ), context );
    QgsRasterLayer *blueLayer = parameterAsRasterLayer( parameters, QStringLiteral( "BLUE_BAND" ), context );
    QgsRasterLayer *swirLayer = parameterAsRasterLayer( parameters, QStringLiteral( "SWIR_BAND" ), context );

    // Determine which bands are needed based on index type
    // 0=NDVI, 1=EVI, 2=SAVI, 3=NDWI, 4=NDBI, 5=MNDWI
    switch ( indexType )
    {
        case 0: // NDVI
        case 2: // SAVI
            if ( !nirLayer || !nirLayer->dataProvider() )
                throw QgsProcessingException( QObject::tr( "NIR band is required for this index" ) );
            if ( !redLayer || !redLayer->dataProvider() )
                throw QgsProcessingException( QObject::tr( "Red band is required for this index" ) );
            break;
        case 1: // EVI
            if ( !nirLayer || !nirLayer->dataProvider() )
                throw QgsProcessingException( QObject::tr( "NIR band is required for EVI" ) );
            if ( !redLayer || !redLayer->dataProvider() )
                throw QgsProcessingException( QObject::tr( "Red band is required for EVI" ) );
            if ( !blueLayer || !blueLayer->dataProvider() )
                throw QgsProcessingException( QObject::tr( "Blue band is required for EVI" ) );
            break;
        case 3: // NDWI
        case 5: // MNDWI
            if ( !greenLayer || !greenLayer->dataProvider() )
                throw QgsProcessingException( QObject::tr( "Green band is required for this index" ) );
            if ( indexType == 3 && ( !nirLayer || !nirLayer->dataProvider() ) )
                throw QgsProcessingException( QObject::tr( "NIR band is required for NDWI" ) );
            if ( indexType == 5 && ( !swirLayer || !swirLayer->dataProvider() ) )
                throw QgsProcessingException( QObject::tr( "SWIR band is required for MNDWI" ) );
            break;
        case 4: // NDBI
            if ( !swirLayer || !swirLayer->dataProvider() )
                throw QgsProcessingException( QObject::tr( "SWIR band is required for NDBI" ) );
            if ( !nirLayer || !nirLayer->dataProvider() )
                throw QgsProcessingException( QObject::tr( "NIR band is required for NDBI" ) );
            break;
        default:
            throw QgsProcessingException( QObject::tr( "Unknown index type" ) );
    }

    // Use the first available layer as reference for extent/dimensions
    QgsRasterLayer *refLayer = nirLayer ? nirLayer : redLayer ? redLayer : greenLayer;
    if ( !refLayer )
        throw QgsProcessingException( QObject::tr( "No valid reference layer" ) );

    QgsRectangle extent = refLayer->extent();
    int nCols = refLayer->width();
    int nRows = refLayer->height();
    QgsCoordinateReferenceSystem crs = refLayer->crs();

    for ( QgsRasterLayer *l : { nirLayer, redLayer, greenLayer, blueLayer, swirLayer } )
    {
        if ( l && l->crs() != crs )
        {
            throw QgsProcessingException(
                QObject::tr( "CRS mismatch: layer '%1' has CRS '%2', but reference layer has '%3'" )
                    .arg( l->name(), l->crs().authid(), crs.authid() ) );
        }
    }

    if ( nCols <= 0 || nRows <= 0 )
        throw QgsProcessingException( QObject::tr( "Invalid raster dimensions" ) );

    size_t totalPixels = static_cast<size_t>( nCols ) * static_cast<size_t>( nRows );
    feedback->setProgressText( QObject::tr( "Reading input bands..." ) );

    // Read band data as needed
    std::vector<float> nirData, redData, greenData, blueData, swirData;

    if ( nirLayer && nirLayer->dataProvider() )
    {
        nirData = readBandData( nirLayer->dataProvider(), 1, extent, nCols, nRows );
        if ( nirData.empty() )
            throw QgsProcessingException( QObject::tr( "Could not read NIR band" ) );
    }
    feedback->setProgress( 20 );

    if ( redLayer && redLayer->dataProvider() )
    {
        redData = readBandData( redLayer->dataProvider(), 1, extent, nCols, nRows );
        if ( redData.empty() )
            throw QgsProcessingException( QObject::tr( "Could not read Red band" ) );
    }
    feedback->setProgress( 40 );

    if ( greenLayer && greenLayer->dataProvider() )
    {
        greenData = readBandData( greenLayer->dataProvider(), 1, extent, nCols, nRows );
        if ( greenData.empty() )
            throw QgsProcessingException( QObject::tr( "Could not read Green band" ) );
    }
    feedback->setProgress( 50 );

    if ( blueLayer && blueLayer->dataProvider() )
    {
        blueData = readBandData( blueLayer->dataProvider(), 1, extent, nCols, nRows );
        if ( blueData.empty() )
            throw QgsProcessingException( QObject::tr( "Could not read Blue band" ) );
    }

    if ( swirLayer && swirLayer->dataProvider() )
    {
        swirData = readBandData( swirLayer->dataProvider(), 1, extent, nCols, nRows );
        if ( swirData.empty() )
            throw QgsProcessingException( QObject::tr( "Could not read SWIR band" ) );
    }
    feedback->setProgress( 60 );

    // Compute spectral index
    feedback->setProgressText( QObject::tr( "Computing spectral index..." ) );
    std::vector<float> result( totalPixels );
    bool ok = false;

    switch ( indexType )
    {
        case 0: // NDVI
            ok = SpectralIndices::ndvi( nirData.data(), redData.data(), result.data(), totalPixels );
            break;
        case 1: // EVI
            ok = SpectralIndices::evi( nirData.data(), redData.data(), blueData.data(), result.data(), totalPixels );
            break;
        case 2: // SAVI
            ok = SpectralIndices::savi( nirData.data(), redData.data(), result.data(), totalPixels );
            break;
        case 3: // NDWI
            ok = SpectralIndices::ndwi( greenData.data(), nirData.data(), result.data(), totalPixels );
            break;
        case 4: // NDBI
            ok = SpectralIndices::ndbi( swirData.data(), nirData.data(), result.data(), totalPixels );
            break;
        case 5: // MNDWI
            ok = SpectralIndices::mndwi( greenData.data(), swirData.data(), result.data(), totalPixels );
            break;
    }

    if ( !ok )
        throw QgsProcessingException( QObject::tr( "Failed to compute spectral index" ) );

    feedback->setProgress( 80 );

    // Write output raster
    feedback->setProgressText( QObject::tr( "Writing output raster..." ) );
    QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

    QgsRasterFileWriter writer( dest );
    writer.setOutputFormat( QStringLiteral( "GTiff" ) );
    std::unique_ptr<QgsRasterDataProvider> outProvider(
        writer.createOneBandRaster( Qgis::DataType::Float32, nCols, nRows, extent, crs ) );
    if ( !outProvider )
        throw QgsProcessingException( QObject::tr( "Could not create output raster" ) );

    const double outNodata = std::numeric_limits<double>::quiet_NaN();
    outProvider->setNoDataValue( 1, outNodata );

    QgsRasterBlock outBlock( Qgis::DataType::Float32, nCols, nRows );
    outBlock.setNoDataValue( outNodata );
    for ( int row = 0; row < nRows; ++row )
    {
        for ( int col = 0; col < nCols; ++col )
        {
            size_t idx = static_cast<size_t>( row ) * nCols + col;
            float val = result[idx];
            if ( std::isnan( val ) )
                outBlock.setIsNoData( row, col );
            else
                outBlock.setValue( row, col, static_cast<double>( val ) );
        }
    }

    if ( !outProvider->writeBlock( &outBlock, 1 ) )
        throw QgsProcessingException( QObject::tr( "Error writing output raster" ) );

    feedback->setProgress( 100 );

    return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
}

QString SpectralIndexAlgorithm::shortHelpString() const
{
    return QObject::tr( "Computes standard remote sensing vegetation, water, and build-up indices (NDVI, EVI, SAVI, NDWI, NDBI, MNDWI) using red, near-infrared, blue, green, and shortwave infrared bands." );
}

QVariantMap SpectralIndexAlgorithm::metadata() const
{
    return QVariantMap{
        { QStringLiteral( "purpose" ), QObject::tr( "Standard spectral index computation." ) },
        { QStringLiteral( "useCases" ), QStringList{ QObject::tr( "Vegetation health monitoring (NDVI, EVI, SAVI)" ), QObject::tr( "Water body mapping (NDWI, MNDWI)" ), QObject::tr( "Built-up area detection (NDBI)" ) } },
        { QStringLiteral( "prerequisites" ), QStringList{ QObject::tr( "Input raster must contain the bands required for the selected index." ) } },
        { QStringLiteral( "limitations" ), QStringList{ QObject::tr( "Requires correctly mapping red, NIR, blue, green, or SWIR bands depending on the selected index." ) } },
        { QStringLiteral( "workflowHints" ), QStringList{ QObject::tr( "Can be used to generate input features for supervised image classifiers (SVM, Random Forest)." ) } }
    };
}

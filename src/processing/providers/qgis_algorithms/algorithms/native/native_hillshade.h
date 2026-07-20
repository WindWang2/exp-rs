// native_hillshade.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "algorithm_help_catalog.h"
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsrasterlayer.h>
#include <qgsrasterfilewriter.h>
#include <qgsrasterpipe.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterblock.h>
#include <qgsrectangle.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>

#include "algorithms/terrain_analysis.h"

#include <cmath>
#include <algorithm>

class QgsHillshadeAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsHillshadeAlgorithm() = default;
    QString name() const override { return QStringLiteral( "hillshade" ); }
    QString displayName() const override { return QObject::tr( "Hillshade" ); }
    QString group() const override { return QObject::tr( "Raster analysis" ); }
    QString groupId() const override { return QStringLiteral( "rasteranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "hillshade" ), QObject::tr( "terrain" ), QObject::tr( "dem" ), QObject::tr( "shading" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsHillshadeAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
        addParameter( new QgsProcessingParameterNumber( QStringLiteral( "Z_FACTOR" ), QObject::tr( "Z factor" ),
            Qgis::ProcessingNumberParameterType::Double, 1.0, false, 0.0 ) );
        addParameter( new QgsProcessingParameterNumber( QStringLiteral( "SUN_AZIMUTH" ), QObject::tr( "Sun azimuth (degrees)" ),
            Qgis::ProcessingNumberParameterType::Double, 315.0, false, 0.0, 360.0 ) );
        addParameter( new QgsProcessingParameterNumber( QStringLiteral( "SUN_ELEVATION" ), QObject::tr( "Sun elevation (degrees)" ),
            Qgis::ProcessingNumberParameterType::Double, 45.0, false, 0.0, 90.0 ) );
        addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Hillshade" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        QgsRasterLayer *layer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
        if ( !layer || !layer->dataProvider() )
            throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

        const double zFactor = parameterAsDouble( parameters, QStringLiteral( "Z_FACTOR" ), context );
        const double sunAzimuth = parameterAsDouble( parameters, QStringLiteral( "SUN_AZIMUTH" ), context );
        const double sunElevation = parameterAsDouble( parameters, QStringLiteral( "SUN_ELEVATION" ), context );
        QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

        feedback->setProgressText( QObject::tr( "Generating hillshade..." ) );

        const int nCols = layer->width();
        const int nRows = layer->height();
        const double cellSize = layer->rasterUnitsPerPixelX();

        // Read band 1 as float
        QgsRasterDataProvider *dp = layer->dataProvider();
        const float nodata = static_cast<float>( dp->sourceNoDataValue( 1 ) );

        QVector<float> demData( nCols * nRows );
        QVector<float> hsData( nCols * nRows );

        // Read block-by-block for progress reporting
        const int blockSize = 256;
        for ( int row = 0; row < nRows; row += blockSize )
        {
            for ( int col = 0; col < nCols; col += blockSize )
            {
                if ( feedback->isCanceled() )
                    return {};

                const int bw = std::min( blockSize, nCols - col );
                const int bh = std::min( blockSize, nRows - row );
                QgsRectangle blockExtent = layer->extent();
                const double xMin = blockExtent.xMinimum() + col * cellSize;
                const double xMax = xMin + bw * cellSize;
                const double yMax = blockExtent.yMaximum() - row * cellSize;
                const double yMin = yMax - bh * cellSize;
                blockExtent = QgsRectangle( xMin, yMin, xMax, yMax );

                std::unique_ptr<QgsRasterBlock> block( dp->block( 1, blockExtent, bw, bh ) );
                if ( !block )
                    continue;

                for ( int r = 0; r < bh; ++r )
                {
                    for ( int c = 0; c < bw; ++c )
                    {
                        const double val = block->value( r, c );
                        demData[( row + r ) * nCols + ( col + c )] = std::isnan( val ) ? nodata : static_cast<float>( val );
                    }
                }
            }
            feedback->setProgress( 50.0 * row / nRows );
        }

        // Apply z-factor
        if ( std::abs( zFactor - 1.0 ) > 1e-6 )
        {
            for ( int i = 0; i < demData.size(); ++i )
            {
                if ( demData[i] != nodata && !std::isnan( demData[i] ) )
                    demData[i] *= static_cast<float>( zFactor );
            }
        }

        // Compute hillshade
        if ( !TerrainAnalysis::hillshade( demData.constData(), hsData.data(), nCols, nRows,
                                          static_cast<float>( cellSize ), nodata,
                                          static_cast<float>( sunAzimuth ), static_cast<float>( sunElevation ) ) )
        {
            throw QgsProcessingException( QObject::tr( "Hillshade computation failed" ) );
        }

        if ( feedback->isCanceled() )
            return {};

        feedback->setProgress( 60 );

        // Write output raster
        QgsRasterFileWriter writer( dest );
        writer.setOutputFormat( "GTiff" );

        QgsRasterDataProvider *outDp = writer.createOneBandRaster(
            Qgis::DataType::Float32, nCols, nRows, layer->extent(), layer->crs() );
        if ( !outDp )
        {
            throw QgsProcessingException( QObject::tr( "Could not create output raster" ) );
        }

        // Write hillshade data block-by-block
        for ( int row = 0; row < nRows; row += blockSize )
        {
            for ( int col = 0; col < nCols; col += blockSize )
            {
                if ( feedback->isCanceled() )
                {
                    delete outDp;
                    return {};
                }
                const int bw = std::min( blockSize, nCols - col );
                const int bh = std::min( blockSize, nRows - row );

                QgsRasterBlock outBlock( Qgis::DataType::Float32, bw, bh );
                for ( int r = 0; r < bh; ++r )
                {
                    for ( int c = 0; c < bw; ++c )
                    {
                        outBlock.setValue( r, c, static_cast<double>( hsData[( row + r ) * nCols + ( col + c )] ) );
                    }
                }
                outDp->writeBlock( &outBlock, 1, col, row );
            }
            feedback->setProgress( 60 + 40.0 * row / nRows );
        }
        delete outDp;

        feedback->setProgress( 100 );
        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// native_clip_raster.h
#pragma once

#include <memory>
#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsrasterlayer.h>
#include <qgsrasterfilewriter.h>
#include <qgsrasterpipe.h>
#include <qgsrasterprojector.h>
#include <qgsrasterdataprovider.h>
#include <qgsrectangle.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>

class QgsClipRasterByExtentAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsClipRasterByExtentAlgorithm() = default;
    QString name() const override { return QStringLiteral( "cliprasterbyextent" ); }
    QString displayName() const override { return QObject::tr( "Clip Raster by Extent" ); }
    QString group() const override { return QObject::tr( "Raster analysis" ); }
    QString groupId() const override { return QStringLiteral( "rasteranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "clip" ), QObject::tr( "raster" ), QObject::tr( "extent" ), QObject::tr( "crop" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsClipRasterByExtentAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
        addParameter( new QgsProcessingParameterExtent( QStringLiteral( "EXTENT" ), QObject::tr( "Extent" ) ) );
        addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Clipped raster" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        QgsRasterLayer *layer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
        if ( !layer || !layer->dataProvider() )
            throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

        QgsRectangle extent = parameterAsExtent( parameters, QStringLiteral( "EXTENT" ), context );
        QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

        feedback->setProgressText( QObject::tr( "Clipping raster..." ) );

        int nCols = static_cast<int>( extent.width() / layer->rasterUnitsPerPixelX() );
        int nRows = static_cast<int>( extent.height() / layer->rasterUnitsPerPixelY() );

        if ( nCols <= 0 || nRows <= 0 )
            throw QgsProcessingException( QObject::tr( "Invalid extent for clipping" ) );

        QgsRasterFileWriter writer( dest );
        writer.setOutputFormat( "GTiff" );

        auto pipe = std::make_unique<QgsRasterPipe>();
        if ( !pipe->set( layer->dataProvider()->clone() ) )
        {
            throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );
        }

        QgsRasterProjector *projector = new QgsRasterProjector();
        projector->setCrs( layer->crs(), layer->crs() );
        pipe->insert( 2, projector );

        Qgis::RasterFileWriterResult err = writer.writeRaster( pipe.get(), nCols, nRows, extent, layer->crs(), context.transformContext() );

        if ( err != Qgis::RasterFileWriterResult::Success )
            throw QgsProcessingException( QObject::tr( "Error writing clipped raster" ) );

        feedback->setProgress( 100 );

        QVariantMap results;
        results[QStringLiteral( "OUTPUT" )] = dest;
        return results;
    }
};

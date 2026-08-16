// native_raster_statistics.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <QFile>
#include <QTextStream>

class QgsRasterLayerStatisticsAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsRasterLayerStatisticsAlgorithm() = default;
    QString name() const override { return QStringLiteral( "rasterlayerstatistics" ); }
    QString displayName() const override { return QObject::tr( "Raster Layer Statistics" ); }
    QString group() const override { return QObject::tr( "Raster analysis" ); }
    QString groupId() const override { return QStringLiteral( "rasteranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "statistics" ), QObject::tr( "raster" ), QObject::tr( "min" ), QObject::tr( "max" ), QObject::tr( "mean" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsRasterLayerStatisticsAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
        addParameter( new QgsProcessingParameterFileDestination( QStringLiteral( "OUTPUT_HTML" ), QObject::tr( "Statistics" ),
            QObject::tr( "HTML files (*.html)" ), QVariant(), true ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        QgsRasterLayer *layer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
        if ( !layer || !layer->dataProvider() )
            throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

        feedback->setProgressText( QObject::tr( "Calculating statistics..." ) );

        QgsRasterBandStats stats = layer->dataProvider()->bandStatistics( 1,
            Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max |
            Qgis::RasterBandStatistic::Mean | Qgis::RasterBandStatistic::StdDev );

        QString html;
        html += QStringLiteral( "<html><body>" );
        html += QStringLiteral( "<h2>Raster Layer Statistics: %1</h2>" ).arg( layer->name() );
        html += QStringLiteral( "<table border='1' cellpadding='4'>" );
        html += QStringLiteral( "<tr><td>Minimum</td><td>%1</td></tr>" ).arg( stats.minimumValue );
        html += QStringLiteral( "<tr><td>Maximum</td><td>%1</td></tr>" ).arg( stats.maximumValue );
        html += QStringLiteral( "<tr><td>Mean</td><td>%1</td></tr>" ).arg( stats.mean );
        html += QStringLiteral( "<tr><td>Std Dev</td><td>%1</td></tr>" ).arg( stats.stdDev );
        html += QStringLiteral( "</table></body></html>" );

        QVariantMap results;
        if ( parameters.contains( QStringLiteral( "OUTPUT_HTML" ) ) && !parameterAsFileOutput( parameters, QStringLiteral( "OUTPUT_HTML" ), context ).isEmpty() )
        {
            QString dest = parameterAsFileOutput( parameters, QStringLiteral( "OUTPUT_HTML" ), context );
            QFile file( dest );
            if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
            {
                throw QgsProcessingException( QObject::tr( "Cannot write output file: %1" ).arg( dest ) );
            }
            QTextStream ts( &file );
            ts << html;
            file.close();
            results[QStringLiteral( "OUTPUT_HTML" )] = dest;
        }

        feedback->setProgress( 100 );
        return results;
    }
};

// src/processing/providers/qgis_algorithms/algorithms/raster/raster_statistics.cpp
#include "raster_statistics.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterinterface.h>
#include <qgsrectangle.h>

#include <QFile>
#include <QTextStream>

void RasterStatisticsAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
    addParameter( new QgsProcessingParameterFileDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Statistics" ),
        QObject::tr( "Text files (*.txt)" ), QVariant(), true ) );
}

QVariantMap RasterStatisticsAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QgsRasterLayer *layer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
    if ( !layer || !layer->dataProvider() )
        throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

    feedback->setProgressText( QObject::tr( "Calculating statistics..." ) );

    QgsRasterDataProvider *provider = layer->dataProvider();
    int bandCount = provider->bandCount();

    QString output;
    QVariantMap results;

    QStringList lines;
    lines << QStringLiteral( "Raster Statistics: %1" ).arg( layer->name() );
    lines << QStringLiteral( "Bands: %1" ).arg( bandCount );
    lines << QStringLiteral( "" );

    for ( int band = 1; band <= bandCount; ++band )
    {
        if ( feedback->isCanceled() )
            break;

        feedback->setProgress( 100.0 * ( band - 1 ) / bandCount );

        QgsRasterBandStats stats = provider->bandStatistics( band,
            Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max |
            Qgis::RasterBandStatistic::Mean | Qgis::RasterBandStatistic::StdDev );

        lines << QStringLiteral( "Band %1:" ).arg( band );
        lines << QStringLiteral( "  Minimum: %1" ).arg( stats.minimumValue );
        lines << QStringLiteral( "  Maximum: %1" ).arg( stats.maximumValue );
        lines << QStringLiteral( "  Mean: %1" ).arg( stats.mean );
        lines << QStringLiteral( "  Std Dev: %1" ).arg( stats.stdDev );
        lines << QStringLiteral( "" );
    }

    feedback->setProgress( 100 );

    if ( parameters.contains( QStringLiteral( "OUTPUT" ) ) &&
         !parameterAsFileOutput( parameters, QStringLiteral( "OUTPUT" ), context ).isEmpty() )
    {
        output = parameterAsFileOutput( parameters, QStringLiteral( "OUTPUT" ), context );
        QFile file( output );
        if ( file.open( QIODevice::WriteOnly | QIODevice::Text ) )
        {
            QTextStream ts( &file );
            for ( const QString &line : lines )
                ts << line << "\n";
            file.close();
        }
        results[QStringLiteral( "OUTPUT" )] = output;
    }

    return results;
}

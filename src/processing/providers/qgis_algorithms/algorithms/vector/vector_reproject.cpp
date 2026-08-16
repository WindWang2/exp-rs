// src/processing/providers/qgis_algorithms/algorithms/vector/vector_reproject.cpp
#include "vector_reproject.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsexception.h>
#include <qgswkbtypes.h>

const QString VectorReprojectAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorReprojectAlgorithm::TARGET_CRS = QStringLiteral( "TARGET_CRS" );
const QString VectorReprojectAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorReprojectAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
    addParameter( new QgsProcessingParameterCrs( TARGET_CRS, QObject::tr( "Target CRS" ),
        QStringLiteral( "EPSG:4326" ) ) );
    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Reprojected" ) ) );
}

QVariantMap VectorReprojectAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    QgsCoordinateReferenceSystem targetCrs = parameterAsCrs( parameters, TARGET_CRS, context );
    QgsCoordinateTransform transform( source->sourceCrs(), targetCrs, context.transformContext() );

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), source->wkbType(), targetCrs ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    QgsFeatureIterator it = source->getFeatures();
    QgsFeature feat;
    long long total = source->featureCount();
    long long current = 0;

    while ( it.nextFeature( feat ) )
    {
        if ( feedback->isCanceled() )
            break;

        current++;
        if ( total > 0 )
            feedback->setProgress( 100.0 * current / total );

        if ( feat.hasGeometry() )
        {
            QgsFeature outputFeat = feat;
            QgsGeometry geom = feat.geometry();
            try
            {
                geom.transform( transform );
            }
            catch ( const QgsCsException &e )
            {
                if ( feedback )
                    feedback->reportError( QObject::tr( "Could not transform geometry with id %1: %2" ).arg( feat.id() ).arg( e.what() ) );
                continue;
            }
            outputFeat.setGeometry( geom );
            sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
        }
        else
        {
            sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }
    }

    return QVariantMap{{OUTPUT, dest}};
}

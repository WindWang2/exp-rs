// src/processing/providers/qgis_algorithms/algorithms/vector/vector_difference.cpp
#include "vector_difference.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgswkbtypes.h>

const QString VectorDifferenceAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorDifferenceAlgorithm::OVERLAY = QStringLiteral( "OVERLAY" );
const QString VectorDifferenceAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorDifferenceAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    addParameter( new QgsProcessingParameterFeatureSource( OVERLAY, QObject::tr( "Difference layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Difference" ) ) );
}

QVariantMap VectorDifferenceAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    std::unique_ptr<QgsProcessingFeatureSource> overlaySource( parameterAsSource( parameters, OVERLAY, context ) );
    if ( !overlaySource )
        throw QgsProcessingException( invalidSourceError( parameters, OVERLAY ) );

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), Qgis::WkbType::Unknown, source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    // Combine all overlay geometries into a single geometry
    QgsGeometry overlayCombined;
    QgsFeatureIterator overlayIt = overlaySource->getFeatures();
    QgsFeature overlayFeat;
    while ( overlayIt.nextFeature( overlayFeat ) )
    {
        if ( feedback->isCanceled() )
            break;
        if ( overlayFeat.hasGeometry() )
        {
            if ( overlayCombined.isNull() )
                overlayCombined = overlayFeat.geometry();
            else
                overlayCombined = overlayCombined.combine( overlayFeat.geometry() );
        }
    }

    if ( overlayCombined.isNull() )
    {
        // No overlay geometry, pass all input features through
        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() )
                break;
            sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }
        return QVariantMap{{OUTPUT, dest}};
    }

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
            QgsGeometry diff = feat.geometry().difference( overlayCombined );
            if ( !diff.isEmpty() )
            {
                QgsFeature outputFeat = feat;
                outputFeat.setGeometry( diff );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }
    }

    return QVariantMap{{OUTPUT, dest}};
}

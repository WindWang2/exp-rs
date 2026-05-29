// src/processing/providers/qgis_algorithms/algorithms/vector/vector_clip.cpp
#include "vector_clip.h"

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

const QString VectorClipAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorClipAlgorithm::OVERLAY = QStringLiteral( "OVERLAY" );
const QString VectorClipAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorClipAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
    addParameter( new QgsProcessingParameterFeatureSource( OVERLAY, QObject::tr( "Overlay layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Clipped" ) ) );
}

QVariantMap VectorClipAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    std::unique_ptr<QgsProcessingFeatureSource> overlay( parameterAsSource( parameters, OVERLAY, context ) );
    if ( !overlay )
        throw QgsProcessingException( invalidSourceError( parameters, OVERLAY ) );

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), Qgis::WkbType::Unknown, source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    // Combine overlay geometries into a single clip geometry
    QgsGeometry clipGeom;
    QgsFeatureIterator overlayIt = overlay->getFeatures();
    QgsFeature overlayFeat;
    while ( overlayIt.nextFeature( overlayFeat ) )
    {
        if ( feedback->isCanceled() )
            break;
        if ( overlayFeat.hasGeometry() )
        {
            if ( clipGeom.isNull() )
                clipGeom = overlayFeat.geometry();
            else
                clipGeom = clipGeom.combine( overlayFeat.geometry() );
        }
    }

    if ( clipGeom.isNull() )
        return QVariantMap{{OUTPUT, dest}};

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
            QgsGeometry clipped = feat.geometry().intersection( clipGeom );
            if ( !clipped.isEmpty() )
            {
                QgsFeature outputFeat = feat;
                outputFeat.setGeometry( clipped );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }
    }

    return QVariantMap{{OUTPUT, dest}};
}

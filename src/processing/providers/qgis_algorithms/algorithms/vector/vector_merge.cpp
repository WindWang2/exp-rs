// src/processing/providers/qgis_algorithms/algorithms/vector/vector_merge.cpp
#include "vector_merge.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsmaplayer.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgswkbtypes.h>

const QString VectorMergeAlgorithm::INPUT_LAYERS = QStringLiteral( "INPUT_LAYERS" );
const QString VectorMergeAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorMergeAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterMultipleLayers( INPUT_LAYERS, QObject::tr( "Input layers" ),
        Qgis::ProcessingSourceType::VectorAnyGeometry ) );
    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Merged" ) ) );
}

QVariantMap VectorMergeAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QList<QgsMapLayer *> mapLayers = parameterAsLayerList( parameters, INPUT_LAYERS, context );
    if ( mapLayers.isEmpty() )
    {
        if ( QgsVectorLayer *singleVl = parameterAsVectorLayer( parameters, INPUT_LAYERS, context ) )
            mapLayers.append( singleVl );
    }

    // Extract valid QgsVectorLayer pointers
    QList<QgsVectorLayer *> layers;
    for ( QgsMapLayer *layer : mapLayers )
    {
        if ( feedback->isCanceled() )
            break;

        QgsVectorLayer *vl = qobject_cast<QgsVectorLayer *>( layer );
        if ( vl && vl->isValid() && vl->geometryType() != Qgis::GeometryType::Null )
            layers.append( vl );
    }

    if ( layers.isEmpty() )
        throw QgsProcessingException( QObject::tr( "No valid input layers found" ) );

    // Use the first layer to set up the sink
    QString dest;
    QgsFields outputFields = layers.first()->fields();
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        outputFields, layers.first()->wkbType(), layers.first()->crs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    // Copy features from all input layers
    long long totalFeatures = 0;
    for ( QgsVectorLayer *vl : layers )
        totalFeatures += vl->featureCount();

    long long current = 0;
    for ( QgsVectorLayer *vl : layers )
    {
        if ( feedback->isCanceled() )
            break;

        QgsFeatureIterator it = vl->getFeatures();
        QgsFeature feat;
        const QgsFields inFields = vl->fields();
        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() )
                break;

            current++;
            if ( totalFeatures > 0 )
                feedback->setProgress( 100.0 * current / totalFeatures );

            QgsFeature outFeat( outputFields );
            outFeat.setGeometry( feat.geometry() );
            const QgsAttributes inAttrs = feat.attributes();
            for ( int i = 0; i < inFields.count() && i < inAttrs.count(); ++i )
            {
                int outIdx = outputFields.indexOf( inFields.at( i ).name() );
                if ( outIdx >= 0 )
                    outFeat.setAttribute( outIdx, inAttrs.at( i ) );
            }
            sink->addFeature( outFeat, QgsFeatureSink::FastInsert );
        }
    }

    feedback->setProgress( 100 );
    return QVariantMap{{OUTPUT, dest}};
}

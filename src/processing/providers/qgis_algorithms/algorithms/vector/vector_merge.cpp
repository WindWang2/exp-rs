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
    QVariant layersVar = parameters.value( INPUT_LAYERS );
    if ( !layersVar.isValid() )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT_LAYERS ) );

    QVariantList layerList = layersVar.toList();
    if ( layerList.isEmpty() )
        throw QgsProcessingException( QObject::tr( "No input layers provided" ) );

    // Extract QgsVectorLayer pointers from the variant list
    QList<QgsVectorLayer *> layers;
    for ( const QVariant &layerVar : layerList )
    {
        if ( feedback->isCanceled() )
            break;

        QgsVectorLayer *vl = qobject_cast<QgsVectorLayer *>( qvariant_cast<QgsMapLayer *>( layerVar ) );
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
        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() )
                break;

            current++;
            if ( totalFeatures > 0 )
                feedback->setProgress( 100.0 * current / totalFeatures );

            sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }
    }

    feedback->setProgress( 100 );
    return QVariantMap{{OUTPUT, dest}};
}

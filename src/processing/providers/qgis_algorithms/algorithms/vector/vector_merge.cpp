// src/processing/providers/qgis_algorithms/algorithms/vector/vector_merge.cpp
#include "vector_merge.h"

#include "../../algorithm_write_guards.h"

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

    // Use the first layer to set up the sink. Declared before the sink so the
    // guard's destructor runs after the sink has flushed and closed the file.
    sicnu::qgis_algorithms::PartialOutputGuard destGuard;
    QString dest;
    QgsFields outputFields = layers.first()->fields();
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        outputFields, layers.first()->wkbType(), layers.first()->crs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );
    destGuard.arm( dest );

    // Copy features from all input layers
    long long totalFeatures = 0;
    for ( QgsVectorLayer *vl : layers )
        totalFeatures += vl->featureCount();

    long long current = 0;
    const QgsCoordinateReferenceSystem targetCrs = layers.first()->crs();
    for ( QgsVectorLayer *vl : layers )
    {
        // Cancellation must abort the run, not return a partial merge (#1043).
        sicnu::qgis_algorithms::checkCanceled( feedback );

        const bool needsTransform = vl->crs().isValid() && targetCrs.isValid() &&
                                    vl->crs() != targetCrs;
        QgsCoordinateTransform ct;
        if ( needsTransform )
        {
            ct = QgsCoordinateTransform( vl->crs(), targetCrs, context.transformContext() );
        }

        QgsFeatureIterator it = vl->getFeatures();
        QgsFeature feat;
        const QgsFields inFields = vl->fields();
        while ( it.nextFeature( feat ) )
        {
            sicnu::qgis_algorithms::checkCanceled( feedback );

            current++;
            if ( totalFeatures > 0 )
                feedback->setProgress( 100.0 * current / totalFeatures );

            QgsFeature outFeat( outputFields );
            QgsGeometry g = feat.geometry();
            if ( needsTransform && feat.hasGeometry() )
            {
                try
                {
                    g.transform( ct );
                }
                catch ( const QgsCsException & ) {}
            }
            outFeat.setGeometry( g );
            const QgsAttributes inAttrs = feat.attributes();
            for ( int i = 0; i < inFields.count() && i < inAttrs.count(); ++i )
            {
                int outIdx = outputFields.indexOf( inFields.at( i ).name() );
                if ( outIdx >= 0 )
                    outFeat.setAttribute( outIdx, inAttrs.at( i ) );
            }
            // A rejected feature must fail the run: the sink is typed from the
            // first layer, so merging e.g. points into a polygon sink cannot
            // silently drop the mismatching layer (#1043).
            sicnu::qgis_algorithms::addFeatureChecked( sink.get(), outFeat, feedback );
        }
    }

    sicnu::qgis_algorithms::flushSinkChecked( sink.get() );
    destGuard.disarm();

    feedback->setProgress( 100 );
    return QVariantMap{{OUTPUT, dest}};
}

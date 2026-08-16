// src/processing/providers/qgis_algorithms/algorithms/vector/vector_distance_matrix.cpp
#include "vector_distance_matrix.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsspatialindex.h>
#include <qgswkbtypes.h>
#include <cmath>

const QString VectorDistanceMatrixAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorDistanceMatrixAlgorithm::TARGET_LAYER = QStringLiteral( "TARGET_LAYER" );
const QString VectorDistanceMatrixAlgorithm::INPUT_FIELD = QStringLiteral( "INPUT_FIELD" );
const QString VectorDistanceMatrixAlgorithm::TARGET_FIELD = QStringLiteral( "TARGET_FIELD" );
const QString VectorDistanceMatrixAlgorithm::OUTPUT_TYPE = QStringLiteral( "OUTPUT_TYPE" );
const QString VectorDistanceMatrixAlgorithm::NEAREST_ONLY = QStringLiteral( "NEAREST_ONLY" );
const QString VectorDistanceMatrixAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorDistanceMatrixAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input point layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorPoint ) ) );

    addParameter( new QgsProcessingParameterFeatureSource( TARGET_LAYER, QObject::tr( "Target point layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorPoint ) ) );

    addParameter( new QgsProcessingParameterField( INPUT_FIELD, QObject::tr( "Input unique ID field" ),
        QVariant(), INPUT, Qgis::ProcessingFieldParameterDataType::Any ) );

    addParameter( new QgsProcessingParameterField( TARGET_FIELD, QObject::tr( "Target unique ID field" ),
        QVariant(), TARGET_LAYER, Qgis::ProcessingFieldParameterDataType::Any ) );

    QStringList outputTypes;
    outputTypes << QObject::tr( "Linear (N * K x 4)" )
                << QObject::tr( "Standard (N x M)" );
    addParameter( new QgsProcessingParameterEnum( OUTPUT_TYPE, QObject::tr( "Output matrix type" ),
        outputTypes, false, 0 ) );

    addParameter( new QgsProcessingParameterBoolean( NEAREST_ONLY, QObject::tr( "Use only nearest (k=1)" ),
        false ) );

    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Distance matrix" ) ) );
}

QVariantMap VectorDistanceMatrixAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    std::unique_ptr<QgsProcessingFeatureSource> targetSource( parameterAsSource( parameters, TARGET_LAYER, context ) );
    if ( !targetSource )
        throw QgsProcessingException( invalidSourceError( parameters, TARGET_LAYER ) );

    QString inputFieldName = parameterAsString( parameters, INPUT_FIELD, context );
    QString targetFieldName = parameterAsString( parameters, TARGET_FIELD, context );
    int outputType = parameterAsEnum( parameters, OUTPUT_TYPE, context );
    bool nearestOnly = parameterAsBool( parameters, NEAREST_ONLY, context );

    // Build spatial index and store features of target layer
    QgsSpatialIndex targetIndex;
    std::map<QgsFeatureId, QgsFeature> targetFeatures;
    QgsFeatureIterator targetIt = targetSource->getFeatures();
    QgsFeature targetFeat;
    const bool needsTransform = targetSource->sourceCrs().isValid() && source->sourceCrs().isValid() &&
                                targetSource->sourceCrs() != source->sourceCrs();
    QgsCoordinateTransform ct;
    if ( needsTransform )
    {
        ct = QgsCoordinateTransform( targetSource->sourceCrs(), source->sourceCrs(), context.transformContext() );
    }

    while ( targetIt.nextFeature( targetFeat ) )
    {
        if ( feedback->isCanceled() )
            break;
        if ( targetFeat.hasGeometry() )
        {
            if ( needsTransform )
            {
                try
                {
                    QgsGeometry g = targetFeat.geometry();
                    g.transform( ct );
                    targetFeat.setGeometry( g );
                }
                catch ( const QgsCsException & ) {}
            }
            targetIndex.addFeature( targetFeat );
            targetFeatures[targetFeat.id()] = targetFeat;
        }
    }

    // Linear output: always produces linear format (input_id, target_id, distance, rank)
    QgsFields outputFields;
    outputFields.append( QgsField( QStringLiteral( "InputID" ), QMetaType::Type::QString ) );
    outputFields.append( QgsField( QStringLiteral( "TargetID" ), QMetaType::Type::QString ) );
    outputFields.append( QgsField( QStringLiteral( "Distance" ), QMetaType::Type::Double ) );

    if ( !nearestOnly && outputType == 0 )
    {
        outputFields.append( QgsField( QStringLiteral( "Rank" ), QMetaType::Type::Int ) );
    }

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        outputFields, Qgis::WkbType::NoGeometry, source->sourceCrs() ) );
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

        if ( !feat.hasGeometry() )
            continue;

        QString inputId = feat.attribute( inputFieldName ).toString();
        QgsPointXY sourcePoint = feat.geometry().centroid().asPoint();

        if ( nearestOnly )
        {
            // Find only the nearest target feature
            QgsFeatureId nearestId = targetIndex.nearestNeighbor( sourcePoint, 1 ).value( 0, FID_NULL );
            if ( nearestId != FID_NULL )
            {
                const QgsFeature &nearest = targetFeatures[nearestId];
                QgsPointXY targetPoint = nearest.geometry().centroid().asPoint();
                double dx = sourcePoint.x() - targetPoint.x();
                double dy = sourcePoint.y() - targetPoint.y();
                double dist = std::sqrt( dx * dx + dy * dy );

                QgsFeature outputFeat;
                outputFeat.setFields( outputFields );
                outputFeat.setAttribute( QStringLiteral( "InputID" ), inputId );
                outputFeat.setAttribute( QStringLiteral( "TargetID" ), nearest.attribute( targetFieldName ).toString() );
                outputFeat.setAttribute( QStringLiteral( "Distance" ), dist );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }
        else
        {
            // Compute distances to all target features
            struct DistEntry
            {
                QString targetId;
                double distance;
            };
            std::vector<DistEntry> distances;

            for ( const auto &pair : targetFeatures )
            {
                QgsPointXY targetPoint = pair.second.geometry().centroid().asPoint();
                double dx = sourcePoint.x() - targetPoint.x();
                double dy = sourcePoint.y() - targetPoint.y();
                double dist = std::sqrt( dx * dx + dy * dy );
                distances.push_back( {pair.second.attribute( targetFieldName ).toString(), dist} );
            }

            // Sort by distance for ranking
            std::sort( distances.begin(), distances.end(),
                []( const DistEntry &a, const DistEntry &b ) { return a.distance < b.distance; } );

            int rank = 1;
            for ( const auto &de : distances )
            {
                QgsFeature outputFeat;
                outputFeat.setFields( outputFields );
                outputFeat.setAttribute( QStringLiteral( "InputID" ), inputId );
                outputFeat.setAttribute( QStringLiteral( "TargetID" ), de.targetId );
                outputFeat.setAttribute( QStringLiteral( "Distance" ), de.distance );
                if ( outputType == 0 )
                    outputFeat.setAttribute( QStringLiteral( "Rank" ), rank );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
                rank++;
            }
        }
    }

    return QVariantMap{{OUTPUT, dest}};
}

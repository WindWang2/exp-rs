// src/processing/providers/qgis_algorithms/algorithms/vector/vector_nearest_neighbor.cpp
#include "vector_nearest_neighbor.h"

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

const QString VectorNearestNeighborAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorNearestNeighborAlgorithm::REFERENCE_LAYER = QStringLiteral( "REFERENCE_LAYER" );
const QString VectorNearestNeighborAlgorithm::MAX_DISTANCE = QStringLiteral( "MAX_DISTANCE" );
const QString VectorNearestNeighborAlgorithm::K_NEIGHBORS = QStringLiteral( "K_NEIGHBORS" );
const QString VectorNearestNeighborAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorNearestNeighborAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    addParameter( new QgsProcessingParameterFeatureSource( REFERENCE_LAYER, QObject::tr( "Reference layer (nearest features from)" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    addParameter( new QgsProcessingParameterNumber( K_NEIGHBORS, QObject::tr( "Number of neighbors" ),
        Qgis::ProcessingNumberParameterType::Integer, 1, false, 1 ) );

    addParameter( new QgsProcessingParameterNumber( MAX_DISTANCE, QObject::tr( "Maximum search distance (0 = unlimited)" ),
        Qgis::ProcessingNumberParameterType::Double, 0.0, true, 0.0 ) );

    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Nearest neighbor" ) ) );
}

QVariantMap VectorNearestNeighborAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    std::unique_ptr<QgsProcessingFeatureSource> refSource( parameterAsSource( parameters, REFERENCE_LAYER, context ) );
    if ( !refSource )
        throw QgsProcessingException( invalidSourceError( parameters, REFERENCE_LAYER ) );

    int kNeighbors = parameterAsInt( parameters, K_NEIGHBORS, context );
    double maxDistance = parameterAsDouble( parameters, MAX_DISTANCE, context );

    if ( kNeighbors < 1 )
        throw QgsProcessingException( QObject::tr( "Number of neighbors must be at least 1" ) );

    // Create output fields: original fields + distance + neighbor_id
    QgsFields outputFields = source->fields();
    outputFields.append( QgsField( QStringLiteral( "distance" ), QVariant::Double ) );
    outputFields.append( QgsField( QStringLiteral( "neighbor_id" ), QVariant::LongLong ) );

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        outputFields, Qgis::WkbType::Point, source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    // Build spatial index and store geometries of reference layer
    QgsSpatialIndex refIndex;
    std::map<QgsFeatureId, QgsGeometry> refGeometries;
    QgsFeatureIterator refIt = refSource->getFeatures();
    QgsFeature refFeat;
    while ( refIt.nextFeature( refFeat ) )
    {
        if ( feedback->isCanceled() )
            break;
        if ( refFeat.hasGeometry() )
        {
            refIndex.addFeature( refFeat );
            refGeometries[refFeat.id()] = refFeat.geometry();
        }
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

        if ( !feat.hasGeometry() )
            continue;

        QgsPointXY sourcePoint = feat.geometry().centroid().asPoint();

        // Use spatial index for candidate search
        QgsRectangle searchRect;
        if ( maxDistance > 0 )
        {
            searchRect = QgsRectangle( sourcePoint.x() - maxDistance, sourcePoint.y() - maxDistance,
                                       sourcePoint.x() + maxDistance, sourcePoint.y() + maxDistance );
        }
        else
        {
            searchRect = refIndex.geometry( refFeat.id() ).boundingBox();
            // Use a very large search area
            searchRect = QgsRectangle( -1e15, -1e15, 1e15, 1e15 );
        }

        auto candidateIds = refIndex.intersects( searchRect );

        // Calculate distances to all candidates
        struct NeighborDist
        {
            QgsFeatureId id;
            double distance;
        };
        std::vector<NeighborDist> distances;

        for ( QgsFeatureId fid : candidateIds )
        {
            const QgsGeometry &refGeom = refGeometries[fid];
            QgsPointXY refPoint = refGeom.centroid().asPoint();

            double dx = sourcePoint.x() - refPoint.x();
            double dy = sourcePoint.y() - refPoint.y();
            double dist = std::sqrt( dx * dx + dy * dy );

            if ( maxDistance > 0 && dist > maxDistance )
                continue;

            distances.push_back( {fid, dist} );
        }

        // Sort by distance
        std::sort( distances.begin(), distances.end(),
            []( const NeighborDist &a, const NeighborDist &b ) { return a.distance < b.distance; } );

        // Output up to k nearest neighbors
        int count = 0;
        for ( const auto &nd : distances )
        {
            if ( count >= kNeighbors )
                break;

            QgsFeature outputFeat;
            outputFeat.setFields( outputFields );
            outputFeat.setGeometry( refGeometries[nd.id] );
            outputFeat.setAttribute( QStringLiteral( "distance" ), nd.distance );
            outputFeat.setAttribute( QStringLiteral( "neighbor_id" ), static_cast<qlonglong>( nd.id ) );

            // Copy input attributes
            for ( int i = 0; i < source->fields().count(); i++ )
            {
                outputFeat.setAttribute( source->fields().at( i ).name(), feat.attribute( i ) );
            }

            sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            count++;
        }
    }

    return QVariantMap{{OUTPUT, dest}};
}

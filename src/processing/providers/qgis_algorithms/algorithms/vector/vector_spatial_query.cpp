// src/processing/providers/qgis_algorithms/algorithms/vector/vector_spatial_query.cpp
#include "vector_spatial_query.h"

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

const QString VectorSpatialQueryAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorSpatialQueryAlgorithm::INTERSECT = QStringLiteral( "INTERSECT" );
const QString VectorSpatialQueryAlgorithm::PREDICATE = QStringLiteral( "PREDICATE" );
const QString VectorSpatialQueryAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorSpatialQueryAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
    addParameter( new QgsProcessingParameterFeatureSource( INTERSECT, QObject::tr( "Intersect layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    QStringList predicates;
    predicates << QObject::tr( "intersects" ) << QObject::tr( "contains" )
               << QObject::tr( "within" ) << QObject::tr( "crosses" )
               << QObject::tr( "touches" ) << QObject::tr( "overlaps" );
    addParameter( new QgsProcessingParameterEnum( PREDICATE, QObject::tr( "Spatial predicate" ),
        predicates, false, 0 ) );

    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Selected" ) ) );
}

QVariantMap VectorSpatialQueryAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    std::unique_ptr<QgsProcessingFeatureSource> intersectSource( parameterAsSource( parameters, INTERSECT, context ) );
    if ( !intersectSource )
        throw QgsProcessingException( invalidSourceError( parameters, INTERSECT ) );

    int predicateIdx = parameterAsEnum( parameters, PREDICATE, context );

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), source->wkbType(), source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    // Build spatial index and geometry map on intersect layer
    QgsSpatialIndex spatialIndex;
    std::map<QgsFeatureId, QgsGeometry> intersectGeometries;
    QgsFeatureIterator intersectIt = intersectSource->getFeatures();
    QgsFeature intersectFeat;
    const bool needsTransform = intersectSource->sourceCrs().isValid() && source->sourceCrs().isValid() &&
                                intersectSource->sourceCrs() != source->sourceCrs();
    QgsCoordinateTransform ct;
    if ( needsTransform )
    {
        ct = QgsCoordinateTransform( intersectSource->sourceCrs(), source->sourceCrs(), context.transformContext() );
    }

    while ( intersectIt.nextFeature( intersectFeat ) )
    {
        if ( feedback->isCanceled() )
            break;
        if ( intersectFeat.hasGeometry() )
        {
            QgsGeometry g = intersectFeat.geometry();
            if ( needsTransform )
            {
                try
                {
                    g.transform( ct );
                    intersectFeat.setGeometry( g );
                }
                catch ( const QgsCsException & ) {}
            }
            spatialIndex.addFeature( intersectFeat );
            intersectGeometries[intersectFeat.id()] = g;
        }
    }

    // Query input features against the spatial index
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

        // Use intersects for initial filtering
        auto intersectIds = spatialIndex.intersects( feat.geometry().boundingBox() );

        bool match = false;
        for ( QgsFeatureId fid : intersectIds )
        {
            auto geomIt = intersectGeometries.find( fid );
            if ( geomIt == intersectGeometries.end() || geomIt->second.isNull() )
                continue;
            const QgsGeometry &interGeom = geomIt->second;

            switch ( predicateIdx )
            {
                case 0: // intersects
                    match = feat.geometry().intersects( interGeom );
                    break;
                case 1: // contains
                    match = feat.geometry().contains( interGeom );
                    break;
                case 2: // within
                    match = feat.geometry().within( interGeom );
                    break;
                case 3: // crosses
                    match = feat.geometry().crosses( interGeom );
                    break;
                case 4: // touches
                    match = feat.geometry().touches( interGeom );
                    break;
                case 5: // overlaps
                    match = feat.geometry().overlaps( interGeom );
                    break;
                case 6: // equals
                    match = feat.geometry().equals( interGeom );
                    break;
            }
            if ( match )
                break;
        }

        if ( match )
            sink->addFeature( feat, QgsFeatureSink::FastInsert );
    }

    return QVariantMap{{OUTPUT, dest}};
}

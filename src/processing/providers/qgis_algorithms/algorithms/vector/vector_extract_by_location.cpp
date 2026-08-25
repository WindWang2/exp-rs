// src/processing/providers/qgis_algorithms/algorithms/vector/vector_extract_by_location.cpp
#include "vector_extract_by_location.h"

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

const QString VectorExtractByLocationAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorExtractByLocationAlgorithm::INTERSECT = QStringLiteral( "INTERSECT" );
const QString VectorExtractByLocationAlgorithm::PREDICATE = QStringLiteral( "PREDICATE" );
const QString VectorExtractByLocationAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorExtractByLocationAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Extract features from" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    addParameter( new QgsProcessingParameterFeatureSource( INTERSECT, QObject::tr( "By comparing to the features from" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    QStringList predicates;
    predicates << QObject::tr( "intersect" )
               << QObject::tr( "are within" )
               << QObject::tr( "contain" )
               << QObject::tr( "equal" )
               << QObject::tr( "touch" )
               << QObject::tr( "overlap" )
               << QObject::tr( "cross" )
               << QObject::tr( "are disjoint" );
    addParameter( new QgsProcessingParameterEnum( PREDICATE, QObject::tr( "Where the features" ),
        predicates, false, 0 ) );

    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Extracted (location)" ) ) );
}

QVariantMap VectorExtractByLocationAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
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

    // Build spatial index on intersect layer
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
                catch ( const QgsCsException & )
                {
                    if ( feedback )
                        feedback->reportError( QObject::tr( "Failed to transform intersect feature %1 geometry" ).arg( intersectFeat.id() ) );
                    continue;
                }
            }
            spatialIndex.addFeature( intersectFeat );
            intersectGeometries[intersectFeat.id()] = g;
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

        auto candidateIds = spatialIndex.intersects( feat.geometry().boundingBox() );

        bool match = false;
        if ( predicateIdx == 7 ) // disjoint
        {
            match = true;
            for ( QgsFeatureId fid : candidateIds )
            {
                const QgsGeometry &interGeom = intersectGeometries[fid];
                if ( feat.geometry().intersects( interGeom ) )
                {
                    match = false;
                    break;
                }
            }
        }
        else
        {
            for ( QgsFeatureId fid : candidateIds )
            {
                const QgsGeometry &interGeom = intersectGeometries[fid];
                switch ( predicateIdx )
                {
                    case 0: match = feat.geometry().intersects( interGeom ); break;
                    case 1: match = feat.geometry().within( interGeom ); break;
                    case 2: match = feat.geometry().contains( interGeom ); break;
                    case 3: match = feat.geometry().equals( interGeom ); break;
                    case 4: match = feat.geometry().touches( interGeom ); break;
                    case 5: match = feat.geometry().overlaps( interGeom ); break;
                    case 6: match = feat.geometry().crosses( interGeom ); break;
                }
                if ( match )
                    break;
            }
        }

        if ( match )
            sink->addFeature( feat, QgsFeatureSink::FastInsert );
    }

    return QVariantMap{{OUTPUT, dest}};
}

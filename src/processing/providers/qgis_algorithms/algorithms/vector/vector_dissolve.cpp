// src/processing/providers/qgis_algorithms/algorithms/vector/vector_dissolve.cpp
#include "vector_dissolve.h"

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

#include <QMap>

const QString VectorDissolveAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorDissolveAlgorithm::FIELD = QStringLiteral( "FIELD" );
const QString VectorDissolveAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorDissolveAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
    addParameter( new QgsProcessingParameterField( FIELD, QObject::tr( "Dissolve field" ),
        QVariant(), INPUT ) );
    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Dissolved" ) ) );
}

QVariantMap VectorDissolveAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    QString fieldName = parameterAsString( parameters, FIELD, context );
    int fieldIdx = source->fields().indexOf( fieldName );
    if ( fieldIdx < 0 )
        throw QgsProcessingException( QObject::tr( "Field '%1' not found" ).arg( fieldName ) );

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), source->wkbType(), source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    // Group geometries by field value. Collect the raw parts per group and
    // union each group ONCE with GEOS: the historical per-feature combine()
    // re-copied the accumulated geometry on every iteration (O(n²) copying,
    // #1056).
    QMap<QVariant, QVector<QgsGeometry>> geomGroups;
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
            geomGroups[feat.attribute( fieldIdx )].append( feat.geometry() );
        }
    }

    // Write dissolved features
    auto it2 = geomGroups.constBegin();
    for ( ; it2 != geomGroups.constEnd(); ++it2 )
    {
        QgsGeometry merged = QgsGeometry::unaryUnion( it2.value() );
        if ( merged.isNull() )
        {
            // GEOS refused (invalid input): fall back to a collection so no
            // group is silently dropped.
            merged = QgsGeometry::collectGeometry( it2.value() );
        }
        QgsFeature outputFeat;
        // initAttributes: the vendored setFields defaults to NOT initializing
        // the attribute storage — without this, setAttribute() below is a
        // silent out-of-range no-op and the dissolved output carries a NULL
        // group field on every feature (#1056 follow-up).
        outputFeat.setFields( source->fields(), /*initAttributes=*/true );
        outputFeat.setAttribute( fieldIdx, it2.key() );
        outputFeat.setGeometry( merged );
        if ( !sink->addFeature( outputFeat, QgsFeatureSink::FastInsert ) )
            throw QgsProcessingException( writeFeatureError( sink.get(), parameters, QString() ) );
    }

    return QVariantMap{{OUTPUT, dest}};
}

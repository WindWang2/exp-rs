// src/processing/providers/qgis_algorithms/algorithms/vector/vector_dissolve.cpp
#include "vector_dissolve.h"

#include "../../algorithm_write_guards.h"

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
#include <QVector>

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

    sicnu::qgis_algorithms::PartialOutputGuard destGuard;
    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), source->wkbType(), source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );
    destGuard.arm( dest );

    // Group input geometries per field value first and union each group in one
    // unaryUnion() call. The previous accumulate-with-combine() loop re-unioned
    // the growing group geometry once per feature — an O(group²) copy/union
    // pattern (#1056).
    QMap<QVariant, QVector<QgsGeometry> > grouped;
    QgsFeatureIterator it = source->getFeatures();
    QgsFeature feat;
    long long total = source->featureCount();
    long long current = 0;

    while ( it.nextFeature( feat ) )
    {
        sicnu::qgis_algorithms::checkCanceled( feedback );

        current++;
        if ( total > 0 )
            feedback->setProgress( 50.0 * current / total );

        if ( feat.hasGeometry() )
            grouped[feat.attribute( fieldIdx )].append( feat.geometry() );
    }

    // Write dissolved features
    int groupIndex = 0;
    auto it2 = grouped.constBegin();
    for ( ; it2 != grouped.constEnd(); ++it2, ++groupIndex )
    {
        sicnu::qgis_algorithms::checkCanceled( feedback );

        const QgsGeometry dissolved = QgsGeometry::unaryUnion( it2.value() );
        if ( dissolved.isNull() && !it2.value().isEmpty() )
        {
            throw QgsProcessingException( QObject::tr( "Failed to dissolve %1 geometries of group '%2'" )
                                              .arg( it2.value().size() )
                                              .arg( it2.key().toString() ) );
        }

        QgsFeature outputFeat;
        outputFeat.setFields( source->fields(), true );
        outputFeat.setAttribute( fieldIdx, it2.key() );
        outputFeat.setGeometry( dissolved );
        sicnu::qgis_algorithms::addFeatureChecked( sink.get(), outputFeat, feedback );

        if ( total > 0 )
            feedback->setProgress( 50.0 + 50.0 * groupIndex / grouped.size() );
    }

    sicnu::qgis_algorithms::flushSinkChecked( sink.get() );
    destGuard.disarm();

    feedback->setProgress( 100 );
    return QVariantMap{{OUTPUT, dest}};
}

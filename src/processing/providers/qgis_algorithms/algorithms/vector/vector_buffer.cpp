// src/processing/providers/qgis_algorithms/algorithms/vector/vector_buffer.cpp
#include "vector_buffer.h"

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

const QString VectorBufferAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorBufferAlgorithm::DISTANCE = QStringLiteral( "DISTANCE" );
const QString VectorBufferAlgorithm::CAP_STYLE = QStringLiteral( "CAP_STYLE" );
const QString VectorBufferAlgorithm::SEGMENTS = QStringLiteral( "SEGMENTS" );
const QString VectorBufferAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorBufferAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    addParameter( new QgsProcessingParameterNumber( DISTANCE, QObject::tr( "Distance" ),
        Qgis::ProcessingNumberParameterType::Double, 100.0, false ) );

    QStringList capStyles;
    capStyles << QObject::tr( "Round" ) << QObject::tr( "Flat" ) << QObject::tr( "Square" );
    addParameter( new QgsProcessingParameterEnum( CAP_STYLE, QObject::tr( "Cap style" ),
        capStyles, false, 0 ) );

    addParameter( new QgsProcessingParameterNumber( SEGMENTS, QObject::tr( "Segments" ),
        Qgis::ProcessingNumberParameterType::Integer, 25, false, 1 ) );

    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Buffered" ) ) );
}

QVariantMap VectorBufferAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    double distance = parameterAsDouble( parameters, DISTANCE, context );
    int capStyle = parameterAsEnum( parameters, CAP_STYLE, context );
    int segments = parameterAsInt( parameters, SEGMENTS, context );

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), Qgis::WkbType::MultiPolygon, source->sourceCrs() ) );
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

        if ( feat.hasGeometry() )
        {
            Qgis::EndCapStyle endCap = Qgis::EndCapStyle::Round;
            if ( capStyle == 1 )
                endCap = Qgis::EndCapStyle::Flat;
            else if ( capStyle == 2 )
                endCap = Qgis::EndCapStyle::Square;

            QgsFeature outputFeat = feat;
            outputFeat.setGeometry( feat.geometry().buffer( distance, segments, endCap, Qgis::JoinStyle::Round, 2.0 ) );
            sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
        }
    }

    return QVariantMap{{OUTPUT, dest}};
}

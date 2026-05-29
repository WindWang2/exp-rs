// src/processing/providers/qgis_algorithms/algorithms/vector/vector_attribute_query.cpp
#include "vector_attribute_query.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsfields.h>
#include <qgsexpression.h>
#include <qgsexpressioncontext.h>
#include <qgsexpressioncontextutils.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgswkbtypes.h>

const QString VectorAttributeQueryAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorAttributeQueryAlgorithm::EXPRESSION = QStringLiteral( "EXPRESSION" );
const QString VectorAttributeQueryAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorAttributeQueryAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
    addParameter( new QgsProcessingParameterString( EXPRESSION, QObject::tr( "Expression" ),
        QVariant(), false, true ) );
    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Filtered" ) ) );
}

QVariantMap VectorAttributeQueryAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    QString expressionString = parameterAsString( parameters, EXPRESSION, context );
    if ( expressionString.isEmpty() )
        throw QgsProcessingException( QObject::tr( "Expression cannot be empty" ) );

    QgsExpression expression( expressionString );
    if ( expression.hasParserError() )
        throw QgsProcessingException( QObject::tr( "Expression error: %1" ).arg( expression.parserErrorString() ) );

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), source->wkbType(), source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    QgsExpressionContext exprContext;
    exprContext.appendScope( QgsExpressionContextUtils::globalScope() );
    exprContext.appendScope( QgsExpressionContextUtils::projectScope( context.project() ) );
    expression.prepare( &exprContext );

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

        exprContext.setFeature( feat );

        QVariant result = expression.evaluate( &exprContext );
        if ( !expression.hasEvalError() && result.toBool() )
            sink->addFeature( feat, QgsFeatureSink::FastInsert );
    }

    return QVariantMap{{OUTPUT, dest}};
}

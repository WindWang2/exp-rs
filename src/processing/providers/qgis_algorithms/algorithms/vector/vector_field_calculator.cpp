// src/processing/providers/qgis_algorithms/algorithms/vector/vector_field_calculator.cpp
#include "vector_field_calculator.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsexpression.h>
#include <qgsexpressioncontext.h>
#include <qgsexpressioncontextutils.h>
#include <qgswkbtypes.h>

const QString VectorFieldCalculatorAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorFieldCalculatorAlgorithm::FIELD_NAME = QStringLiteral( "FIELD_NAME" );
const QString VectorFieldCalculatorAlgorithm::FIELD_TYPE = QStringLiteral( "FIELD_TYPE" );
const QString VectorFieldCalculatorAlgorithm::FIELD_LENGTH = QStringLiteral( "FIELD_LENGTH" );
const QString VectorFieldCalculatorAlgorithm::FIELD_PRECISION = QStringLiteral( "FIELD_PRECISION" );
const QString VectorFieldCalculatorAlgorithm::FORMULA = QStringLiteral( "FORMULA" );
const QString VectorFieldCalculatorAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorFieldCalculatorAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    addParameter( new QgsProcessingParameterString( FIELD_NAME, QObject::tr( "Result field name" ),
        QStringLiteral( "new_field" ), false, false ) );

    QStringList fieldTypes;
    fieldTypes << QObject::tr( "Integer" )
               << QObject::tr( "Float" )
               << QObject::tr( "String" );
    addParameter( new QgsProcessingParameterEnum( FIELD_TYPE, QObject::tr( "Field type" ),
        fieldTypes, false, 1 ) );

    addParameter( new QgsProcessingParameterNumber( FIELD_LENGTH, QObject::tr( "Field length" ),
        Qgis::ProcessingNumberParameterType::Integer, 10, false, 1 ) );

    addParameter( new QgsProcessingParameterNumber( FIELD_PRECISION, QObject::tr( "Field precision" ),
        Qgis::ProcessingNumberParameterType::Integer, 3, false, 0 ) );

    addParameter( new QgsProcessingParameterExpression( FORMULA, QObject::tr( "Formula" ),
        QVariant(), INPUT, false ) );

    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Calculated" ) ) );
}

QVariantMap VectorFieldCalculatorAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    QString fieldName = parameterAsString( parameters, FIELD_NAME, context );
    int fieldType = parameterAsEnum( parameters, FIELD_TYPE, context );
    int fieldLength = parameterAsInt( parameters, FIELD_LENGTH, context );
    int fieldPrecision = parameterAsInt( parameters, FIELD_PRECISION, context );
    QString formula = parameterAsExpression( parameters, FORMULA, context );

    if ( fieldName.isEmpty() )
        throw QgsProcessingException( QObject::tr( "Field name is required" ) );

    if ( formula.isEmpty() )
        throw QgsProcessingException( QObject::tr( "Formula is required" ) );

    // Set up expression
    QgsExpression expr( formula );
    if ( expr.hasParserError() )
        throw QgsProcessingException( QObject::tr( "Expression parsing error: %1" ).arg( expr.parserErrorString() ) );

    // Create output fields with new field added
    QgsFields outputFields = source->fields();
    QMetaType::Type metaType;
    switch ( fieldType )
    {
        case 0: metaType = QMetaType::Type::Int; break;
        case 1: metaType = QMetaType::Type::Double; break;
        case 2: metaType = QMetaType::Type::QString; break;
        default: metaType = QMetaType::Type::Double; break;
    }
    outputFields.append( QgsField( fieldName, metaType, QString(), fieldLength, fieldPrecision ) );

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        outputFields, source->wkbType(), source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    // Prepare expression context
    QgsExpressionContext evalContext = context.expressionContext();
    evalContext.appendScopes( QgsExpressionContextUtils::globalProjectLayerScopes( nullptr ) );

    if ( expr.needsGeometry() )
    {
        // Expression needs geometry - we need to prepare for that
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

        evalContext.setFeature( feat );

        QVariant value = expr.evaluate( &evalContext );

        QgsFeature outputFeat = feat;
        outputFeat.setFields( outputFields );
        outputFeat.setAttribute( fieldName, value );

        sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
    }

    return QVariantMap{{OUTPUT, dest}};
}

// native_extract_by_attribute.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgswkbtypes.h>

class QgsExtractByAttributeAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsExtractByAttributeAlgorithm() = default;
    QString name() const override { return QStringLiteral( "extractbyattribute" ); }
    QString displayName() const override { return QObject::tr( "Extract by Attribute" ); }
    QString group() const override { return QObject::tr( "Vector selection" ); }
    QString groupId() const override { return QStringLiteral( "vectorselection" ); }
    QStringList tags() const override { return { QObject::tr( "extract" ), QObject::tr( "filter" ), QObject::tr( "select" ), QObject::tr( "attribute" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsExtractByAttributeAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterField( QStringLiteral( "FIELD" ), QObject::tr( "Field" ), QVariant(), QStringLiteral( "INPUT" ) ) );

        QStringList operators;
        operators << QStringLiteral( "=" )
                  << QStringLiteral( "!=" )
                  << QStringLiteral( ">" )
                  << QStringLiteral( ">=" )
                  << QStringLiteral( "<" )
                  << QStringLiteral( "<=" )
                  << QStringLiteral( "begins with" )
                  << QStringLiteral( "contains" )
                  << QStringLiteral( "is null" )
                  << QStringLiteral( "is not null" );
        addParameter( new QgsProcessingParameterEnum( QStringLiteral( "OPERATOR" ), QObject::tr( "Operator" ), operators, false, 0 ) );
        addParameter( new QgsProcessingParameterString( QStringLiteral( "VALUE" ), QObject::tr( "Value" ), QString(), false, true ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Extracted" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        QString fieldName = parameterAsString( parameters, QStringLiteral( "FIELD" ), context );
        int op = parameterAsEnum( parameters, QStringLiteral( "OPERATOR" ), context );
        QString value = parameterAsString( parameters, QStringLiteral( "VALUE" ), context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );

        int fieldIdx = source->fields().indexOf( fieldName );
        if ( fieldIdx < 0 )
            throw QgsProcessingException( QObject::tr( "Field '%1' not found" ).arg( fieldName ) );

        const auto fieldType = source->fields().at( fieldIdx ).type();
        const bool isNumeric = ( fieldType == QMetaType::Type::Int ||
                                 fieldType == QMetaType::Type::UInt ||
                                 fieldType == QMetaType::Type::LongLong ||
                                 fieldType == QMetaType::Type::ULongLong ||
                                 fieldType == QMetaType::Type::Double ||
                                 fieldType == QMetaType::Type::Float );

        bool valIsNum = false;
        double targetNum = value.toDouble( &valIsNum );

        auto matches = [&]( const QVariant &attr ) -> bool {
            const bool attrIsNull = attr.isNull() || !attr.isValid();
            if ( op == 8 ) // is null
                return attrIsNull;
            if ( op == 9 ) // is not null
                return !attrIsNull;
            if ( attrIsNull )
                return false;

            if ( isNumeric && valIsNum )
            {
                bool attrOk = false;
                double attrNum = attr.toDouble( &attrOk );
                if ( attrOk )
                {
                    switch ( op )
                    {
                        case 0: return std::abs( attrNum - targetNum ) < 1e-9;
                        case 1: return std::abs( attrNum - targetNum ) >= 1e-9;
                        case 2: return attrNum > targetNum;
                        case 3: return attrNum >= targetNum;
                        case 4: return attrNum < targetNum;
                        case 5: return attrNum <= targetNum;
                        default: break;
                    }
                }
            }

            const QString attrStr = attr.toString();
            switch ( op )
            {
                case 0: return attrStr == value;
                case 1: return attrStr != value;
                case 2: return attrStr > value;
                case 3: return attrStr >= value;
                case 4: return attrStr < value;
                case 5: return attrStr <= value;
                case 6: return attrStr.startsWith( value );
                case 7: return attrStr.contains( value );
                default: return false;
            }
        };

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback && feedback->isCanceled() ) break;
            current++;
            if ( total > 0 && feedback ) feedback->setProgress( 100.0 * current / total );

            if ( matches( feat.attribute( fieldIdx ) ) )
                sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

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
        addParameter( new QgsProcessingParameterString( QStringLiteral( "VALUE" ), QObject::tr( "Value" ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Extracted" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        QString fieldName = parameterAsString( parameters, QStringLiteral( "FIELD" ), context );
        QString value = parameterAsString( parameters, QStringLiteral( "VALUE" ), context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );

        int fieldIdx = source->fields().indexOf( fieldName );
        if ( fieldIdx < 0 )
            throw QgsProcessingException( QObject::tr( "Field '%1' not found" ).arg( fieldName ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.attribute( fieldIdx ).toString() == value )
                sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

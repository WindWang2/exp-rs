// native_difference.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "algorithm_help_catalog.h"
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgswkbtypes.h>

class QgsDifferenceAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsDifferenceAlgorithm() = default;
    QString name() const override { return QStringLiteral( "native_difference" ); }
    QString displayName() const override { return QObject::tr( "Difference (Native)" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "difference" ), QObject::tr( "erase" ), QObject::tr( "subtract" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsDifferenceAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "OVERLAY" ), QObject::tr( "Overlay layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Difference" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        std::unique_ptr<QgsProcessingFeatureSource> overlay( parameterAsSource( parameters, QStringLiteral( "OVERLAY" ), context ) );
        if ( !overlay )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "OVERLAY" ) ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), Qgis::WkbType::Unknown, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );

        QgsGeometry overlayCombined;
        QgsFeatureIterator overlayIt = overlay->getFeatures();
        QgsFeature overlayFeat;
        while ( overlayIt.nextFeature( overlayFeat ) )
        {
            if ( overlayFeat.hasGeometry() )
            {
                if ( overlayCombined.isNull() )
                    overlayCombined = overlayFeat.geometry();
                else
                    overlayCombined = overlayCombined.combine( overlayFeat.geometry() );
            }
        }

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                if ( !overlayCombined.isNull() )
                    outputFeat.setGeometry( feat.geometry().difference( overlayCombined ) );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// native_clip.h
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

class QgsClipAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsClipAlgorithm() = default;
    QString name() const override { return QStringLiteral( "native_clip" ); }
    QString displayName() const override { return QObject::tr( "Clip (Native)" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "clip" ), QObject::tr( "cut" ), QObject::tr( "trim" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsClipAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "OVERLAY" ), QObject::tr( "Overlay layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Clipped" ) ) );
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

        QgsGeometry clipGeom;
        QgsFeatureIterator overlayIt = overlay->getFeatures();
        QgsFeature overlayFeat;
        while ( overlayIt.nextFeature( overlayFeat ) )
        {
            if ( overlayFeat.hasGeometry() )
            {
                if ( clipGeom.isNull() )
                    clipGeom = overlayFeat.geometry();
                else
                    clipGeom = clipGeom.combine( overlayFeat.geometry() );
            }
        }

        if ( clipGeom.isNull() )
            return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};

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
                QgsGeometry clipped = feat.geometry().intersection( clipGeom );
                if ( !clipped.isEmpty() )
                {
                    QgsFeature outputFeat = feat;
                    outputFeat.setGeometry( clipped );
                    sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
                }
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

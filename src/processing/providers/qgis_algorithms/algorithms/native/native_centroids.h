// native_centroids.h
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

class QgsCentroidsAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsCentroidsAlgorithm() = default;
    QString name() const override { return QStringLiteral( "centroids" ); }
    QString displayName() const override { return QObject::tr( "Centroids" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "centroid" ), QObject::tr( "center" ), QObject::tr( "point" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsCentroidsAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Centroids" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), Qgis::WkbType::Point, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );

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
                outputFeat.setGeometry( feat.geometry().centroid() );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

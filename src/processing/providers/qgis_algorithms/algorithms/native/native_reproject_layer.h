// native_reproject_layer.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"
#include "../../algorithm_write_guards.h"
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgswkbtypes.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsexception.h>

class QgsReprojectLayerAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsReprojectLayerAlgorithm() = default;
    QString name() const override { return QStringLiteral( "reprojectlayer" ); }
    QString displayName() const override { return QObject::tr( "Reproject Layer" ); }
    QString group() const override { return QObject::tr( "Vector general" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeneral" ); }
    QStringList tags() const override { return { QObject::tr( "reproject" ), QObject::tr( "transform" ), QObject::tr( "crs" ), QObject::tr( "projection" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsReprojectLayerAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterCrs( QStringLiteral( "TARGET_CRS" ), QObject::tr( "Target CRS" ), QStringLiteral( "EPSG:4326" ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Reprojected" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        QgsCoordinateReferenceSystem targetCrs = parameterAsCrs( parameters, QStringLiteral( "TARGET_CRS" ), context );
        QgsCoordinateTransform transform( source->sourceCrs(), targetCrs, context.transformContext() );

        sicnu::qgis_algorithms::PartialOutputGuard destGuard;
        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), source->wkbType(), targetCrs ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );
        destGuard.arm( dest );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            sicnu::qgis_algorithms::checkCanceled( feedback );
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                QgsGeometry geom = feat.geometry();
                try
                {
                    geom.transform( transform );
                }
                catch ( const QgsCsException &e )
                {
                    if ( feedback )
                        feedback->reportError( QObject::tr( "Could not transform geometry with id %1: %2" ).arg( feat.id() ).arg( e.what() ) );
                    continue;
                }
                outputFeat.setGeometry( geom );
                sicnu::qgis_algorithms::addFeatureChecked( sink.get(), outputFeat, feedback );
            }
            else
            {
                sicnu::qgis_algorithms::addFeatureChecked( sink.get(), feat, feedback );
            }
        }

        sicnu::qgis_algorithms::flushSinkChecked( sink.get() );
        destGuard.disarm();

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// native_assign_projection.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
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

class QgsAssignProjectionAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsAssignProjectionAlgorithm() = default;
    QString name() const override { return QStringLiteral( "assignprojection" ); }
    QString displayName() const override { return QObject::tr( "Assign Projection" ); }
    QString group() const override { return QObject::tr( "Vector general" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeneral" ); }
    QStringList tags() const override { return { QObject::tr( "assign" ), QObject::tr( "projection" ), QObject::tr( "crs" ), QObject::tr( "set" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new QgsAssignProjectionAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterCrs( QStringLiteral( "CRS" ), QObject::tr( "CRS" ), QStringLiteral( "EPSG:4326" ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Assigned" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        QgsCoordinateReferenceSystem crs = parameterAsCrs( parameters, QStringLiteral( "CRS" ), context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), source->wkbType(), crs ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }

        feedback->setProgress( 100 );
        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

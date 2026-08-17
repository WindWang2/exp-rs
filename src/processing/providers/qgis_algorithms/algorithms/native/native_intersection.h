// native_intersection.h
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

class QgsIntersectionAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsIntersectionAlgorithm() = default;
    QString name() const override { return QStringLiteral( "native_intersection" ); }
    QString displayName() const override { return QObject::tr( "Intersection (Native)" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "intersection" ), QObject::tr( "overlap" ), QObject::tr( "common" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsIntersectionAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "OVERLAY" ), QObject::tr( "Overlay layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Intersection" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        std::unique_ptr<QgsProcessingFeatureSource> overlay( parameterAsSource( parameters, QStringLiteral( "OVERLAY" ), context ) );
        if ( !overlay )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "OVERLAY" ) ) );

        QgsFields outFields = source->fields();
        const QgsFields overlayFields = overlay->fields();
        QVector<int> overlayFieldMap;
        for ( int i = 0; i < overlayFields.count(); ++i )
        {
            const QgsField f = overlayFields.at( i );
            QString name = f.name();
            if ( outFields.lookupField( name ) >= 0 )
                name = QStringLiteral( "overlay_" ) + name;
            outFields.append( QgsField( name, f.type(), f.typeName(), f.length(), f.precision() ) );
            overlayFieldMap.append( outFields.count() - 1 );
        }

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            outFields, source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );

        QVector<QgsFeature> overlayFeatures;
        QgsFeatureIterator overlayIt = overlay->getFeatures();
        QgsFeature overlayFeat;
        const bool needsTransform = overlay->sourceCrs().isValid() && source->sourceCrs().isValid() &&
                                    overlay->sourceCrs() != source->sourceCrs();
        QgsCoordinateTransform ct;
        if ( needsTransform )
        {
            ct = QgsCoordinateTransform( overlay->sourceCrs(), source->sourceCrs(), context.transformContext() );
        }

        while ( overlayIt.nextFeature( overlayFeat ) )
        {
            if ( overlayFeat.hasGeometry() )
            {
                if ( needsTransform )
                {
                    try
                    {
                        QgsGeometry g = overlayFeat.geometry();
                        g.transform( ct );
                        overlayFeat.setGeometry( g );
                    }
                    catch ( const QgsCsException & ) {}
                }
                overlayFeatures.append( overlayFeat );
            }
        }

        if ( overlayFeatures.isEmpty() )
            return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback && feedback->isCanceled() ) break;
            current++;
            if ( total > 0 && feedback ) feedback->setProgress( 100.0 * current / total );

            if ( !feat.hasGeometry() )
                continue;

            for ( const QgsFeature &ovFeat : overlayFeatures )
            {
                if ( !feat.geometry().intersects( ovFeat.geometry() ) )
                    continue;
                QgsGeometry result = feat.geometry().intersection( ovFeat.geometry() );
                if ( !result.isEmpty() )
                {
                    QgsFeature outputFeat( outFields );
                    outputFeat.setGeometry( result );
                    for ( int i = 0; i < source->fields().count(); ++i )
                        outputFeat.setAttribute( i, feat.attribute( i ) );
                    for ( int i = 0; i < overlayFields.count(); ++i )
                        outputFeat.setAttribute( overlayFieldMap[i], ovFeat.attribute( i ) );
                    sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
                }
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

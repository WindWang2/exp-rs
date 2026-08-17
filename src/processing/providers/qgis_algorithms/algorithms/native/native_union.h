// native_union.h
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

class QgsUnionAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsUnionAlgorithm() = default;
    QString name() const override { return QStringLiteral( "native_union" ); }
    QString displayName() const override { return QObject::tr( "Union (Native)" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "union" ), QObject::tr( "merge" ), QObject::tr( "combine" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsUnionAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "OVERLAY" ), QObject::tr( "Overlay layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Union" ) ) );
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

        // Collect overlay features
        QVector<QgsFeature> overlayFeatures;
        QgsFeatureIterator overlayIt = overlay->getFeatures();
        QgsFeature overlayFeat;
        QgsGeometry inputCombined;
        const bool needsTransform = overlay->sourceCrs().isValid() && source->sourceCrs().isValid() &&
                                    overlay->sourceCrs() != source->sourceCrs();
        QgsCoordinateTransform ct;
        if ( needsTransform )
        {
            ct = QgsCoordinateTransform( overlay->sourceCrs(), source->sourceCrs(), context.transformContext() );
        }

        while ( overlayIt.nextFeature( overlayFeat ) )
        {
            if ( needsTransform && overlayFeat.hasGeometry() )
            {
                QgsGeometry g = overlayFeat.geometry();
                try
                {
                    g.transform( ct );
                    overlayFeat.setGeometry( g );
                }
                catch ( const QgsCsException & ) {}
            }
            overlayFeatures.append( overlayFeat );
        }

        auto makeIntersectionFeature = [&]( const QgsFeature &inFeat, const QgsFeature &ovFeat, const QgsGeometry &geom ) {
            QgsFeature outFeat( outFields );
            outFeat.setGeometry( geom );
            for ( int i = 0; i < source->fields().count(); ++i )
                outFeat.setAttribute( i, inFeat.attribute( i ) );
            for ( int i = 0; i < overlayFields.count(); ++i )
                outFeat.setAttribute( overlayFieldMap[i], ovFeat.attribute( i ) );
            return outFeat;
        };

        auto makeInputRemainderFeature = [&]( const QgsFeature &inFeat, const QgsGeometry &geom ) {
            QgsFeature outFeat( outFields );
            outFeat.setGeometry( geom );
            for ( int i = 0; i < source->fields().count(); ++i )
                outFeat.setAttribute( i, inFeat.attribute( i ) );
            return outFeat;
        };

        auto makeOverlayRemainderFeature = [&]( const QgsFeature &ovFeat, const QgsGeometry &geom ) {
            QgsFeature outFeat( outFields );
            outFeat.setGeometry( geom );
            for ( int i = 0; i < overlayFields.count(); ++i )
                outFeat.setAttribute( overlayFieldMap[i], ovFeat.attribute( i ) );
            return outFeat;
        };

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount() + overlayFeatures.size();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( !feat.hasGeometry() || feat.geometry().isEmpty() )
            {
                QgsFeature outFeat( outFields );
                for ( int i = 0; i < source->fields().count(); ++i )
                    outFeat.setAttribute( i, feat.attribute( i ) );
                sink->addFeature( outFeat, QgsFeatureSink::FastInsert );
                continue;
            }

            QgsGeometry inputGeom = feat.geometry();
            QgsGeometry inputRemainder = inputGeom;

            if ( inputCombined.isNull() )
                inputCombined = inputGeom;
            else
                inputCombined = inputCombined.combine( inputGeom );

            for ( const QgsFeature &ovFeat : overlayFeatures )
            {
                if ( !ovFeat.hasGeometry() || ovFeat.geometry().isEmpty() )
                    continue;

                if ( inputGeom.intersects( ovFeat.geometry() ) )
                {
                    QgsGeometry inter = inputGeom.intersection( ovFeat.geometry() );
                    if ( !inter.isEmpty() )
                    {
                        QgsFeature outF = makeIntersectionFeature( feat, ovFeat, inter );
                        sink->addFeature( outF, QgsFeatureSink::FastInsert );
                        inputRemainder = inputRemainder.difference( inter );
                    }
                }
            }

            if ( !inputRemainder.isEmpty() )
            {
                QgsFeature outF = makeInputRemainderFeature( feat, inputRemainder );
                sink->addFeature( outF, QgsFeatureSink::FastInsert );
            }
        }

        // Add residual overlay geometries
        for ( const QgsFeature &ovFeat : overlayFeatures )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( !ovFeat.hasGeometry() || ovFeat.geometry().isEmpty() )
            {
                QgsFeature outFeat( outFields );
                for ( int i = 0; i < overlayFields.count(); ++i )
                    outFeat.setAttribute( overlayFieldMap[i], ovFeat.attribute( i ) );
                sink->addFeature( outFeat, QgsFeatureSink::FastInsert );
                continue;
            }

            QgsGeometry ovRemainder = ovFeat.geometry();
            if ( !inputCombined.isNull() )
            {
                ovRemainder = ovRemainder.difference( inputCombined );
            }

            if ( !ovRemainder.isEmpty() )
            {
                QgsFeature outF = makeOverlayRemainderFeature( ovFeat, ovRemainder );
                sink->addFeature( outF, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

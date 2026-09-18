// src/processing/providers/qgis_algorithms/algorithms/vector/vector_clip.cpp
#include "vector_clip.h"

#include "../../algorithm_write_guards.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgswkbtypes.h>

const QString VectorClipAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorClipAlgorithm::OVERLAY = QStringLiteral( "OVERLAY" );
const QString VectorClipAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorClipAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
    addParameter( new QgsProcessingParameterFeatureSource( OVERLAY, QObject::tr( "Overlay layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Clipped" ) ) );
}

QVariantMap VectorClipAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    std::unique_ptr<QgsProcessingFeatureSource> overlay( parameterAsSource( parameters, OVERLAY, context ) );
    if ( !overlay )
        throw QgsProcessingException( invalidSourceError( parameters, OVERLAY ) );

    sicnu::qgis_algorithms::PartialOutputGuard destGuard;
    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), Qgis::WkbType::Unknown, source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );
    destGuard.arm( dest );

    // Combine overlay geometries into a single clip geometry
    QgsGeometry clipGeom;
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
        sicnu::qgis_algorithms::checkCanceled( feedback );
        if ( overlayFeat.hasGeometry() )
        {
            QgsGeometry g = overlayFeat.geometry();
            if ( needsTransform )
            {
                try
                {
                    g.transform( ct );
                }
                catch ( const QgsCsException &e )
                {
                    feedback->reportError( QObject::tr( "Could not transform feature to the target CRS: %1 — skipping" ).arg( e.what() ) );
                    continue;
                }
            }
            if ( clipGeom.isNull() )
                clipGeom = g;
            else
                clipGeom = clipGeom.combine( g );
        }
    }

    if ( clipGeom.isNull() )
    {
        sicnu::qgis_algorithms::flushSinkChecked( sink.get() );
        destGuard.disarm();
        return QVariantMap{{OUTPUT, dest}};
    }

    QgsFeatureIterator it = source->getFeatures();
    QgsFeature feat;
    long long total = source->featureCount();
    long long current = 0;

    while ( it.nextFeature( feat ) )
    {
        sicnu::qgis_algorithms::checkCanceled( feedback );

        current++;
        if ( total > 0 )
            feedback->setProgress( 100.0 * current / total );

        if ( feat.hasGeometry() )
        {
            QgsGeometry clipped = feat.geometry().intersection( clipGeom );
            if ( !clipped.isEmpty() )
            {
                QgsFeature outputFeat = feat;
                outputFeat.setGeometry( clipped );
                sicnu::qgis_algorithms::addFeatureChecked( sink.get(), outputFeat, feedback );
            }
        }
    }

    sicnu::qgis_algorithms::flushSinkChecked( sink.get() );
    destGuard.disarm();

    return QVariantMap{{OUTPUT, dest}};
}

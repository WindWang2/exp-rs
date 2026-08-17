// src/processing/providers/qgis_algorithms/algorithms/vector/vector_symmetrical_difference.cpp
#include "vector_symmetrical_difference.h"

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

const QString VectorSymmetricalDifferenceAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString VectorSymmetricalDifferenceAlgorithm::OVERLAY = QStringLiteral( "OVERLAY" );
const QString VectorSymmetricalDifferenceAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

void VectorSymmetricalDifferenceAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    addParameter( new QgsProcessingParameterFeatureSource( OVERLAY, QObject::tr( "Difference layer" ),
        QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

    addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Symmetrical Difference" ) ) );
}

QVariantMap VectorSymmetricalDifferenceAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
    if ( !source )
        throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

    std::unique_ptr<QgsProcessingFeatureSource> overlaySource( parameterAsSource( parameters, OVERLAY, context ) );
    if ( !overlaySource )
        throw QgsProcessingException( invalidSourceError( parameters, OVERLAY ) );

    QgsFields outFields = source->fields();
    const QgsFields overlayFields = overlaySource->fields();
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
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        outFields, source->wkbType(), source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    // Combine all overlay geometries and store features
    QgsGeometry overlayCombined;
    QVector<QgsFeature> overlayFeatures;
    QgsFeatureIterator overlayIt = overlaySource->getFeatures();
    QgsFeature overlayFeat;
    const bool needsTransform = overlaySource->sourceCrs().isValid() && source->sourceCrs().isValid() &&
                                overlaySource->sourceCrs() != source->sourceCrs();
    QgsCoordinateTransform ct;
    if ( needsTransform )
    {
        ct = QgsCoordinateTransform( overlaySource->sourceCrs(), source->sourceCrs(), context.transformContext() );
    }

    while ( overlayIt.nextFeature( overlayFeat ) )
    {
        if ( feedback && feedback->isCanceled() )
            break;
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
            if ( overlayCombined.isNull() )
                overlayCombined = overlayFeat.geometry();
            else
                overlayCombined = overlayCombined.combine( overlayFeat.geometry() );
        }
    }

    // Combine all input geometries and store features
    QgsGeometry inputCombined;
    QVector<QgsFeature> inputFeatures;
    QgsFeatureIterator inputIt = source->getFeatures();
    QgsFeature inputFeat;
    while ( inputIt.nextFeature( inputFeat ) )
    {
        if ( feedback && feedback->isCanceled() )
            break;
        if ( inputFeat.hasGeometry() )
        {
            inputFeatures.append( inputFeat );
            if ( inputCombined.isNull() )
                inputCombined = inputFeat.geometry();
            else
                inputCombined = inputCombined.combine( inputFeat.geometry() );
        }
    }

    // Part A: features from input that don't intersect overlay
    for ( const QgsFeature &inFeat : inputFeatures )
    {
        if ( feedback && feedback->isCanceled() )
            break;

        QgsGeometry geomA;
        if ( overlayCombined.isNull() )
            geomA = inFeat.geometry();
        else
            geomA = inFeat.geometry().difference( overlayCombined );

        if ( !geomA.isNull() && !geomA.isEmpty() )
        {
            QgsFeature outFeat( outFields );
            outFeat.setGeometry( geomA );
            for ( int i = 0; i < source->fields().count(); ++i )
                outFeat.setAttribute( i, inFeat.attribute( i ) );
            sink->addFeature( outFeat, QgsFeatureSink::FastInsert );
        }
    }

    // Part B: features from overlay that don't intersect input
    for ( const QgsFeature &ovFeat : overlayFeatures )
    {
        if ( feedback && feedback->isCanceled() )
            break;

        QgsGeometry geomB;
        if ( inputCombined.isNull() )
            geomB = ovFeat.geometry();
        else
            geomB = ovFeat.geometry().difference( inputCombined );

        if ( !geomB.isNull() && !geomB.isEmpty() )
        {
            QgsFeature outFeat( outFields );
            outFeat.setGeometry( geomB );
            for ( int i = 0; i < overlayFields.count(); ++i )
                outFeat.setAttribute( overlayFieldMap[i], ovFeat.attribute( i ) );
            sink->addFeature( outFeat, QgsFeatureSink::FastInsert );
        }
    }

    if ( feedback )
        feedback->setProgress( 100 );
    return QVariantMap{{OUTPUT, dest}};
}

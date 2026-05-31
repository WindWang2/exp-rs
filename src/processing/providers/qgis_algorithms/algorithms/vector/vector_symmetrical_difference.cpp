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

    QString dest;
    std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
        source->fields(), Qgis::WkbType::Unknown, source->sourceCrs() ) );
    if ( !sink )
        throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

    // Combine all overlay geometries
    QgsGeometry overlayCombined;
    QgsFeatureIterator overlayIt = overlaySource->getFeatures();
    QgsFeature overlayFeat;
    while ( overlayIt.nextFeature( overlayFeat ) )
    {
        if ( feedback->isCanceled() )
            break;
        if ( overlayFeat.hasGeometry() )
        {
            if ( overlayCombined.isNull() )
                overlayCombined = overlayFeat.geometry();
            else
                overlayCombined = overlayCombined.combine( overlayFeat.geometry() );
        }
    }

    // Combine all input geometries
    QgsGeometry inputCombined;
    QgsFeatureIterator inputIt = source->getFeatures();
    QgsFeature inputFeat;
    while ( inputIt.nextFeature( inputFeat ) )
    {
        if ( feedback->isCanceled() )
            break;
        if ( inputFeat.hasGeometry() )
        {
            if ( inputCombined.isNull() )
                inputCombined = inputFeat.geometry();
            else
                inputCombined = inputCombined.combine( inputFeat.geometry() );
        }
    }

    if ( inputCombined.isNull() && overlayCombined.isNull() )
        return QVariantMap{{OUTPUT, dest}};

    // Part A: features from input that don't intersect overlay
    // Part B: features from overlay that don't intersect input
    // We compute: (input - overlay) union (overlay - input)

    QgsGeometry resultA;
    if ( !inputCombined.isNull() && !overlayCombined.isNull() )
        resultA = inputCombined.difference( overlayCombined );

    QgsGeometry resultB;
    if ( !overlayCombined.isNull() && !inputCombined.isNull() )
        resultB = overlayCombined.difference( inputCombined );

    // Combine the symmetrical difference
    QgsGeometry symDiff;
    if ( !resultA.isNull() && !resultA.isEmpty() )
        symDiff = resultA;
    if ( !resultB.isNull() && !resultB.isEmpty() )
    {
        if ( symDiff.isNull() )
            symDiff = resultB;
        else
            symDiff = symDiff.combine( resultB );
    }

    // If overlay is null, pass through all input features
    if ( overlayCombined.isNull() )
    {
        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() )
                break;
            sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }
    }
    else if ( !symDiff.isNull() && !symDiff.isEmpty() )
    {
        // Output the symmetrical difference as a single feature
        QgsFeature outputFeat;
        outputFeat.setFields( source->fields() );
        outputFeat.setGeometry( symDiff );
        sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
    }

    feedback->setProgress( 100 );
    return QVariantMap{{OUTPUT, dest}};
}

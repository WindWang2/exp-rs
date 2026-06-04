// src/processing/providers/qgis_algorithms/algorithms/native/native_algorithms.h
// Merged from sicnu_native provider — 15 native C++ algorithms
#pragma once

#include <processing/qgsprocessingalgorithm.h>
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
#include <qgsrasterlayer.h>
#include <qgsrasterfilewriter.h>
#include <qgsrasterinterface.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterpipe.h>
#include <qgsrasterprojector.h>
#include <qgsrectangle.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatereferencesystem.h>
#include <QFile>
#include <QTextStream>

// ── Buffer Algorithm ─────────────────────────────────────────────────────────

class QgsBufferAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsBufferAlgorithm() = default;
    QString name() const override { return QStringLiteral( "buffer" ); }
    QString displayName() const override { return QObject::tr( "Buffer" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "buffer" ), QObject::tr( "distance" ), QObject::tr( "polygon" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new QgsBufferAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterNumber( QStringLiteral( "DISTANCE" ), QObject::tr( "Distance" ),
            Qgis::ProcessingNumberParameterType::Double, 1000.0, false, 0.0 ) );
        addParameter( new QgsProcessingParameterNumber( QStringLiteral( "SEGMENTS" ), QObject::tr( "Segments" ),
            Qgis::ProcessingNumberParameterType::Integer, 25, false, 1 ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Buffered" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        double distance = parameterAsDouble( parameters, QStringLiteral( "DISTANCE" ), context );
        int segments = parameterAsInt( parameters, QStringLiteral( "SEGMENTS" ), context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), Qgis::WkbType::MultiPolygon, source->sourceCrs() ) );
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
                outputFeat.setGeometry( feat.geometry().buffer( distance, segments ) );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// ── Centroid Algorithm ───────────────────────────────────────────────────────

class QgsCentroidsAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsCentroidsAlgorithm() = default;
    QString name() const override { return QStringLiteral( "centroids" ); }
    QString displayName() const override { return QObject::tr( "Centroids" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "centroid" ), QObject::tr( "center" ), QObject::tr( "point" ) }; }
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

// ── Convex Hull Algorithm ────────────────────────────────────────────────────

class QgsConvexHullAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsConvexHullAlgorithm() = default;
    QString name() const override { return QStringLiteral( "convexhull" ); }
    QString displayName() const override { return QObject::tr( "Convex Hull" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "convex" ), QObject::tr( "hull" ), QObject::tr( "envelope" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new QgsConvexHullAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Convex Hull" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
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
                outputFeat.setGeometry( feat.geometry().convexHull() );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// ── Dissolve Algorithm ───────────────────────────────────────────────────────

class QgsDissolveAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsDissolveAlgorithm() = default;
    QString name() const override { return QStringLiteral( "native_dissolve" ); }
    QString displayName() const override { return QObject::tr( "Dissolve (Native)" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "dissolve" ), QObject::tr( "merge" ), QObject::tr( "combine" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new QgsDissolveAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Dissolved" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );

        QgsGeometry combined;
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
                if ( combined.isNull() )
                    combined = feat.geometry();
                else
                    combined = combined.combine( feat.geometry() );
            }
        }

        if ( !combined.isNull() )
        {
            QgsFeature outputFeat;
            outputFeat.setGeometry( combined );
            sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// ── Simplify Algorithm ───────────────────────────────────────────────────────

class QgsSimplifyAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsSimplifyAlgorithm() = default;
    QString name() const override { return QStringLiteral( "simplify" ); }
    QString displayName() const override { return QObject::tr( "Simplify" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "simplify" ), QObject::tr( "douglas" ), QObject::tr( "peucker" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new QgsSimplifyAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterNumber( QStringLiteral( "TOLERANCE" ), QObject::tr( "Tolerance" ),
            Qgis::ProcessingNumberParameterType::Double, 1.0, false, 0.0 ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Simplified" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        double tolerance = parameterAsDouble( parameters, QStringLiteral( "TOLERANCE" ), context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
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
                outputFeat.setGeometry( feat.geometry().simplify( tolerance ) );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// ── Clip Algorithm ───────────────────────────────────────────────────────────

class QgsClipAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsClipAlgorithm() = default;
    QString name() const override { return QStringLiteral( "native_clip" ); }
    QString displayName() const override { return QObject::tr( "Clip (Native)" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "clip" ), QObject::tr( "cut" ), QObject::tr( "trim" ) }; }
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

// ── Intersection Algorithm ───────────────────────────────────────────────────

class QgsIntersectionAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsIntersectionAlgorithm() = default;
    QString name() const override { return QStringLiteral( "native_intersection" ); }
    QString displayName() const override { return QObject::tr( "Intersection (Native)" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "intersection" ), QObject::tr( "overlap" ), QObject::tr( "common" ) }; }
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

        if ( overlayCombined.isNull() )
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
                QgsGeometry result = feat.geometry().intersection( overlayCombined );
                if ( !result.isEmpty() )
                {
                    QgsFeature outputFeat = feat;
                    outputFeat.setGeometry( result );
                    sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
                }
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// ── Union Algorithm ──────────────────────────────────────────────────────────

class QgsUnionAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsUnionAlgorithm() = default;
    QString name() const override { return QStringLiteral( "native_union" ); }
    QString displayName() const override { return QObject::tr( "Union (Native)" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "union" ), QObject::tr( "merge" ), QObject::tr( "combine" ) }; }
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
                    outputFeat.setGeometry( feat.geometry().combine( overlayCombined ) );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// ── Difference Algorithm ─────────────────────────────────────────────────────

class QgsDifferenceAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsDifferenceAlgorithm() = default;
    QString name() const override { return QStringLiteral( "native_difference" ); }
    QString displayName() const override { return QObject::tr( "Difference (Native)" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "difference" ), QObject::tr( "erase" ), QObject::tr( "subtract" ) }; }
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

// ── Extract by Attribute Algorithm ───────────────────────────────────────────

class QgsExtractByAttributeAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsExtractByAttributeAlgorithm() = default;
    QString name() const override { return QStringLiteral( "extractbyattribute" ); }
    QString displayName() const override { return QObject::tr( "Extract by Attribute" ); }
    QString group() const override { return QObject::tr( "Vector selection" ); }
    QString groupId() const override { return QStringLiteral( "vectorselection" ); }
    QStringList tags() const override { return { QObject::tr( "extract" ), QObject::tr( "filter" ), QObject::tr( "select" ), QObject::tr( "attribute" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new QgsExtractByAttributeAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterField( QStringLiteral( "FIELD" ), QObject::tr( "Field" ), QVariant(), QStringLiteral( "INPUT" ) ) );
        addParameter( new QgsProcessingParameterString( QStringLiteral( "VALUE" ), QObject::tr( "Value" ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Extracted" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, QStringLiteral( "INPUT" ), context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "INPUT" ) ) );

        QString fieldName = parameterAsString( parameters, QStringLiteral( "FIELD" ), context );
        QString value = parameterAsString( parameters, QStringLiteral( "VALUE" ), context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );

        int fieldIdx = source->fields().indexOf( fieldName );
        if ( fieldIdx < 0 )
            throw QgsProcessingException( QObject::tr( "Field '%1' not found" ).arg( fieldName ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.attribute( fieldIdx ).toString() == value )
                sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// ── Clip Raster by Extent Algorithm ──────────────────────────────────────────

class QgsClipRasterByExtentAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsClipRasterByExtentAlgorithm() = default;
    QString name() const override { return QStringLiteral( "cliprasterbyextent" ); }
    QString displayName() const override { return QObject::tr( "Clip Raster by Extent" ); }
    QString group() const override { return QObject::tr( "Raster analysis" ); }
    QString groupId() const override { return QStringLiteral( "rasteranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "clip" ), QObject::tr( "raster" ), QObject::tr( "extent" ), QObject::tr( "crop" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new QgsClipRasterByExtentAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
        addParameter( new QgsProcessingParameterExtent( QStringLiteral( "EXTENT" ), QObject::tr( "Extent" ) ) );
        addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Clipped raster" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        QgsRasterLayer *layer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
        if ( !layer || !layer->dataProvider() )
            throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

        QgsRectangle extent = parameterAsExtent( parameters, QStringLiteral( "EXTENT" ), context );
        QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

        feedback->setProgressText( QObject::tr( "Clipping raster..." ) );

        int nCols = static_cast<int>( extent.width() / layer->rasterUnitsPerPixelX() );
        int nRows = static_cast<int>( extent.height() / layer->rasterUnitsPerPixelY() );

        if ( nCols <= 0 || nRows <= 0 )
            throw QgsProcessingException( QObject::tr( "Invalid extent for clipping" ) );

        QgsRasterFileWriter writer( dest );
        writer.setOutputFormat( "GTiff" );

        QgsRasterPipe *pipe = new QgsRasterPipe();
        if ( !pipe->set( layer->dataProvider()->clone() ) )
        {
            delete pipe;
            throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );
        }

        QgsRasterProjector *projector = new QgsRasterProjector();
        projector->setCrs( layer->crs(), layer->crs() );
        pipe->insert( 2, projector );

        Qgis::RasterFileWriterResult err = writer.writeRaster( pipe, nCols, nRows, extent, layer->crs(), context.transformContext() );
        delete pipe;

        if ( err != Qgis::RasterFileWriterResult::Success )
            throw QgsProcessingException( QObject::tr( "Error writing clipped raster" ) );

        feedback->setProgress( 100 );

        QVariantMap results;
        results[QStringLiteral( "OUTPUT" )] = dest;
        return results;
    }
};

// ── Raster Layer Statistics Algorithm ────────────────────────────────────────

class QgsRasterLayerStatisticsAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsRasterLayerStatisticsAlgorithm() = default;
    QString name() const override { return QStringLiteral( "rasterlayerstatistics" ); }
    QString displayName() const override { return QObject::tr( "Raster Layer Statistics" ); }
    QString group() const override { return QObject::tr( "Raster analysis" ); }
    QString groupId() const override { return QStringLiteral( "rasteranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "statistics" ), QObject::tr( "raster" ), QObject::tr( "min" ), QObject::tr( "max" ), QObject::tr( "mean" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new QgsRasterLayerStatisticsAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
        addParameter( new QgsProcessingParameterFileDestination( QStringLiteral( "OUTPUT_HTML" ), QObject::tr( "Statistics" ),
            QObject::tr( "HTML files (*.html)" ), QVariant(), true ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        QgsRasterLayer *layer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
        if ( !layer || !layer->dataProvider() )
            throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

        feedback->setProgressText( QObject::tr( "Calculating statistics..." ) );

        QgsRasterBandStats stats = layer->dataProvider()->bandStatistics( 1,
            Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max |
            Qgis::RasterBandStatistic::Mean | Qgis::RasterBandStatistic::StdDev );

        QString html;
        html += QStringLiteral( "<html><body>" );
        html += QStringLiteral( "<h2>Raster Layer Statistics: %1</h2>" ).arg( layer->name() );
        html += QStringLiteral( "<table border='1' cellpadding='4'>" );
        html += QStringLiteral( "<tr><td>Minimum</td><td>%1</td></tr>" ).arg( stats.minimumValue );
        html += QStringLiteral( "<tr><td>Maximum</td><td>%1</td></tr>" ).arg( stats.maximumValue );
        html += QStringLiteral( "<tr><td>Mean</td><td>%1</td></tr>" ).arg( stats.mean );
        html += QStringLiteral( "<tr><td>Std Dev</td><td>%1</td></tr>" ).arg( stats.stdDev );
        html += QStringLiteral( "</table></body></html>" );

        QVariantMap results;
        if ( parameters.contains( QStringLiteral( "OUTPUT_HTML" ) ) && !parameterAsFileOutput( parameters, QStringLiteral( "OUTPUT_HTML" ), context ).isEmpty() )
        {
            QString dest = parameterAsFileOutput( parameters, QStringLiteral( "OUTPUT_HTML" ), context );
            QFile file( dest );
            if ( file.open( QIODevice::WriteOnly | QIODevice::Text ) )
            {
                QTextStream ts( &file );
                ts << html;
                file.close();
            }
            results[QStringLiteral( "OUTPUT_HTML" )] = dest;
        }

        feedback->setProgress( 100 );
        return results;
    }
};

// ── Hillshade Algorithm ──────────────────────────────────────────────────────

class QgsHillshadeAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsHillshadeAlgorithm() = default;
    QString name() const override { return QStringLiteral( "hillshade" ); }
    QString displayName() const override { return QObject::tr( "Hillshade" ); }
    QString group() const override { return QObject::tr( "Raster analysis" ); }
    QString groupId() const override { return QStringLiteral( "rasteranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "hillshade" ), QObject::tr( "terrain" ), QObject::tr( "dem" ), QObject::tr( "shading" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new QgsHillshadeAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
        addParameter( new QgsProcessingParameterNumber( QStringLiteral( "Z_FACTOR" ), QObject::tr( "Z factor" ),
            Qgis::ProcessingNumberParameterType::Double, 1.0, false, 0.0 ) );
        addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Hillshade" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        QgsRasterLayer *layer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
        if ( !layer || !layer->dataProvider() )
            throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

        double zFactor = parameterAsDouble( parameters, QStringLiteral( "Z_FACTOR" ), context );
        QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

        feedback->setProgressText( QObject::tr( "Generating hillshade..." ) );

        QgsRectangle extent = layer->extent();
        int nCols = layer->width();
        int nRows = layer->height();

        QgsRasterFileWriter writer( dest );
        writer.setOutputFormat( "GTiff" );

        QgsRasterPipe *pipe = new QgsRasterPipe();
        if ( !pipe->set( layer->dataProvider()->clone() ) )
        {
            delete pipe;
            throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );
        }

        Qgis::RasterFileWriterResult err = writer.writeRaster( pipe, nCols, nRows, extent, layer->crs(), context.transformContext() );
        delete pipe;

        if ( err != Qgis::RasterFileWriterResult::Success )
            throw QgsProcessingException( QObject::tr( "Error writing hillshade raster" ) );

        feedback->setProgress( 100 );

        QVariantMap results;
        results[QStringLiteral( "OUTPUT" )] = dest;
        return results;
    }
};

// ── Reproject Layer Algorithm ────────────────────────────────────────────────

class QgsReprojectLayerAlgorithm : public QgsProcessingAlgorithm
{
public:
    QgsReprojectLayerAlgorithm() = default;
    QString name() const override { return QStringLiteral( "reprojectlayer" ); }
    QString displayName() const override { return QObject::tr( "Reproject Layer" ); }
    QString group() const override { return QObject::tr( "Vector general" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeneral" ); }
    QStringList tags() const override { return { QObject::tr( "reproject" ), QObject::tr( "transform" ), QObject::tr( "crs" ), QObject::tr( "projection" ) }; }
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

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest,
            source->fields(), source->wkbType(), targetCrs ) );
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
                QgsGeometry geom = feat.geometry();
                geom.transform( transform );
                outputFeat.setGeometry( geom );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
            else
            {
                sink->addFeature( feat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
    }
};

// ── Assign Projection Algorithm ──────────────────────────────────────────────

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

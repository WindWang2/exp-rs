#include "sicnunativealgorithms.h"

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

// ── Buffer Algorithm ─────────────────────────────────────────────────────────

class QgsBufferAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString DISTANCE;
    static const QString SEGMENTS;
    static const QString OUTPUT;

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
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

        addParameter( new QgsProcessingParameterNumber( DISTANCE, QObject::tr( "Distance" ),
            Qgis::ProcessingNumberParameterType::Double, 1000.0, false, 0.0 ) );

        addParameter( new QgsProcessingParameterNumber( SEGMENTS, QObject::tr( "Segments" ),
            Qgis::ProcessingNumberParameterType::Integer, 25, false, 1 ) );

        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Buffered" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        double distance = parameterAsDouble( parameters, DISTANCE, context );
        int segments = parameterAsInt( parameters, SEGMENTS, context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), Qgis::WkbType::MultiPolygon, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() )
                break;

            current++;
            if ( total > 0 )
                feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                outputFeat.setGeometry( feat.geometry().buffer( distance, segments ) );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsBufferAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsBufferAlgorithm::DISTANCE = QStringLiteral( "DISTANCE" );
const QString QgsBufferAlgorithm::SEGMENTS = QStringLiteral( "SEGMENTS" );
const QString QgsBufferAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

// ── Centroid Algorithm ───────────────────────────────────────────────────────

class QgsCentroidsAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OUTPUT;

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
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Centroids" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), Qgis::WkbType::Point, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

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

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsCentroidsAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsCentroidsAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

// ── Convex Hull Algorithm ────────────────────────────────────────────────────

class QgsConvexHullAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OUTPUT;

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
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Convex Hull" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

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

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsConvexHullAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsConvexHullAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

// ── Dissolve Algorithm ───────────────────────────────────────────────────────

class QgsDissolveAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OUTPUT;

    QgsDissolveAlgorithm() = default;

    QString name() const override { return QStringLiteral( "dissolve" ); }
    QString displayName() const override { return QObject::tr( "Dissolve" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "dissolve" ), QObject::tr( "merge" ), QObject::tr( "combine" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsDissolveAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Dissolved" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

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

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsDissolveAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsDissolveAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

// ── Simplify Algorithm ───────────────────────────────────────────────────────

class QgsSimplifyAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString TOLERANCE;
    static const QString OUTPUT;

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
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterNumber( TOLERANCE, QObject::tr( "Tolerance" ),
            Qgis::ProcessingNumberParameterType::Double, 1.0, false, 0.0 ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Simplified" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        double tolerance = parameterAsDouble( parameters, TOLERANCE, context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

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

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsSimplifyAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsSimplifyAlgorithm::TOLERANCE = QStringLiteral( "TOLERANCE" );
const QString QgsSimplifyAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

// ── Provider Implementation ──────────────────────────────────────────────────

SicnuNativeAlgorithms::SicnuNativeAlgorithms( QObject *parent )
    : QgsProcessingProvider( parent )
{
}

QIcon SicnuNativeAlgorithms::icon() const
{
    return QIcon();
}

void SicnuNativeAlgorithms::loadAlgorithms()
{
    addAlgorithm( new QgsBufferAlgorithm() );
    addAlgorithm( new QgsCentroidsAlgorithm() );
    addAlgorithm( new QgsConvexHullAlgorithm() );
    addAlgorithm( new QgsDissolveAlgorithm() );
    addAlgorithm( new QgsSimplifyAlgorithm() );
}

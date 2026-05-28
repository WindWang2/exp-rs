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
}

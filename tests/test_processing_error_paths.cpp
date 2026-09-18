// tests/test_processing_error_paths.cpp — error-path hardening for provider
// algorithms (#1043): sink/write failures must fail the run with a typed
// error, cancellation must abort instead of reporting partial success, and a
// failed/canceled run must not leave a plausible partial output at a
// user-specified path. Also locks in the dissolve union-complexity fix
// (#1056) with a mid-scale perf threshold.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsfeedback.h>
#include <qgsgeometry.h>
#include <qgsprocessingalgorithm.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsvectorlayer.h>

#include "processing/providers/qgis_algorithms/provider.h"
#include "processing/providers/qgis_algorithms/algorithm_write_guards.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_merge.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_dissolve.h"
#include "processing/providers/generic_cli/generic_cli_algorithm.h"
#include "processing/framework/provider_algorithm_adapter.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/qgsprocessingregistry.h"

#include <json/json.h>

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>

#include <stdexcept>
#include <utility>
#include <vector>

using Catch::Approx;
using namespace sicnu::qgis_algorithms;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_processing_error_paths";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    QgsApplication *app = nullptr;
    AppInit()
    {
        static QgsApplication *shared = [] {
            auto *a = new QgsApplication( appArgc(), appArgv, false );
            QgsApplication::initQgis();
            return a;
        }();
        app = shared;
    }
};

// Memory polygon layer with `count` unit squares laid out on a grid starting
// at (originX, originY), each tagged with the given "grp" value.
QgsVectorLayer *makePolygonLayer( const QString &name, const QString &group,
                                  int count, double originX, double originY )
{
    QgsVectorLayer *layer = new QgsVectorLayer(
        QStringLiteral( "Polygon?crs=EPSG:4326&field=grp:string" ), name,
        QStringLiteral( "memory" ) );
    QgsFeatureList features;
    features.reserve( count );
    for ( int i = 0; i < count; ++i )
    {
        const int col = i % 40;
        const int row = i / 40;
        const double x0 = originX + col * 2.0;
        const double y0 = originY + row * 2.0;
        QgsFeature f( layer->fields() );
        f.setAttribute( 0, group );
        f.setGeometry( QgsGeometry::fromRect( QgsRectangle( x0, y0, x0 + 1.0, y0 + 1.0 ) ) );
        features.append( f );
    }
    layer->dataProvider()->addFeatures( features );
    return layer;
}

} // namespace

// ---------------------------------------------------------------------------
// Helpers (unit level)
// ---------------------------------------------------------------------------

namespace
{

class StubSink : public QgsFeatureSink
{
    public:
        bool addFeature( QgsFeature &, QgsFeatureSink::Flags ) override
        {
            mLastError = QStringLiteral( "disk full while writing" );
            return false;
        }
        bool addFeatures( QgsFeatureList &, QgsFeatureSink::Flags ) override { return false; }
        bool flushBuffer() override
        {
            if ( mFailFlush )
            {
                mLastError = QStringLiteral( "flush failed" );
                return false;
            }
            return true;
        }
        QString lastError() const override { return mLastError; }

        bool mFailFlush = false;

    private:
        QString mLastError;
};

} // namespace

TEST_CASE( "addFeatureChecked throws with the sink's last error", "[processing-error][guards]" )
{
    const AppInit app;
    StubSink sink;
    QgsFeature feat;

    REQUIRE_THROWS_AS( addFeatureChecked( &sink, feat ), QgsProcessingException );
    try
    {
        QgsFeature f2;
        addFeatureChecked( &sink, f2 );
        FAIL( "expected throw" );
    }
    catch ( const QgsProcessingException &e )
    {
        // The sink's own diagnostic must reach the typed error.
        REQUIRE( e.what().contains( QStringLiteral( "disk full" ) ) );
    }
}

TEST_CASE( "flushSinkChecked surfaces buffered flush failures", "[processing-error][guards]" )
{
    const AppInit app;
    StubSink sink;
    sink.mFailFlush = true;
    REQUIRE_THROWS_AS( flushSinkChecked( &sink ), QgsProcessingException );
}

TEST_CASE( "checkCanceled throws once the feedback is canceled", "[processing-error][guards]" )
{
    const AppInit app;
    QgsProcessingFeedback fb;
    REQUIRE_NOTHROW( checkCanceled( &fb ) );
    fb.cancel();
    REQUIRE_THROWS_AS( checkCanceled( &fb ), QgsProcessingException );
}

TEST_CASE( "PartialOutputGuard removes armed outputs and sidecars, keeps disarmed ones",
           "[processing-error][guards]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    {
        const QString gpkg = tmp.filePath( QStringLiteral( "out.gpkg" ) );
        { QFile f( gpkg ); f.open( QIODevice::WriteOnly ); }
        { QFile f( gpkg + QStringLiteral( "-wal" ) ); f.open( QIODevice::WriteOnly ); }
        REQUIRE( QFile::exists( gpkg ) );
        {
            PartialOutputGuard guard( gpkg );
        }
        REQUIRE_FALSE( QFile::exists( gpkg ) );
        REQUIRE_FALSE( QFile::exists( gpkg + QStringLiteral( "-wal" ) ) );
    }

    {
        const QString kept = tmp.filePath( QStringLiteral( "kept.tif" ) );
        { QFile f( kept ); f.open( QIODevice::WriteOnly ); }
        {
            PartialOutputGuard guard( kept );
            guard.disarm();
        }
        REQUIRE( QFile::exists( kept ) );
    }

    // Memory layer ids are not file paths — arming must be a no-op.
    {
        const QString layerId = QStringLiteral( "{c1c2c3c4-0000-0000-0000-000000000000}" );
        PartialOutputGuard guard( layerId );
        // Nothing to assert beyond not treating the id as a file path.
        REQUIRE( true );
    }
}

// ---------------------------------------------------------------------------
// vector merge — sink write failure and cancel must not report success
// ---------------------------------------------------------------------------

TEST_CASE( "vector merge fails visibly when a feature cannot be written to the typed sink",
           "[processing-error][merge][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // First layer types the sink: "num" is an integer field. The second layer
    // carries the same field as text with a non-numeric value, so the OGR
    // writer rejects the feature at write time (deterministic sink failure
    // injection; the GPKG driver alone would silently coerce a geometry
    // mismatch).
    QgsVectorLayer *polyLayer = new QgsVectorLayer(
        QStringLiteral( "Polygon?crs=EPSG:4326&field=num:integer" ), QStringLiteral( "polys" ),
        QStringLiteral( "memory" ) );
    {
        QgsFeature f( polyLayer->fields() );
        f.setAttribute( 0, 5 );
        f.setGeometry( QgsGeometry::fromRect( QgsRectangle( 0.0, 0.0, 1.0, 1.0 ) ) );
        QgsFeatureList fl{ f };
        polyLayer->dataProvider()->addFeatures( fl );
    }
    QgsVectorLayer *pointLayer = new QgsVectorLayer(
        QStringLiteral( "Point?crs=EPSG:4326&field=num:string" ), QStringLiteral( "points" ),
        QStringLiteral( "memory" ) );
    {
        QgsFeature f( pointLayer->fields() );
        f.setAttribute( 0, QStringLiteral( "not-a-number" ) );
        f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 2.0, 2.0 ) ) );
        QgsFeatureList fl{ f };
        pointLayer->dataProvider()->addFeatures( fl );
    }

    QgsProcessingContext context;
    context.temporaryLayerStore()->addMapLayer( polyLayer );
    context.temporaryLayerStore()->addMapLayer( pointLayer );

    VectorMergeAlgorithm alg;
    QVariantMap params;
    params.insert( QStringLiteral( "INPUT_LAYERS" ),
                   QVariantList{ polyLayer->id(), pointLayer->id() } );
    const QString out = tmp.filePath( QStringLiteral( "merged.gpkg" ) );
    params.insert( QStringLiteral( "OUTPUT" ), out );

    QgsProcessingFeedback feedback;
    bool ok = true;
    const QVariantMap results = alg.run( params, context, &feedback, &ok );

    // Before the fix the rejected feature was silently dropped and the run
    // reported success with an incomplete merge (#1043).
    REQUIRE_FALSE( ok );
    REQUIRE( results.isEmpty() );
    // The partial output must be removed, not left as a plausible result.
    REQUIRE_FALSE( QFile::exists( out ) );
}

TEST_CASE( "vector merge cancels without leaving a partial output",
           "[processing-error][merge][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    QgsVectorLayer *a = makePolygonLayer( QStringLiteral( "a" ), QStringLiteral( "a" ), 8, 0.0, 0.0 );
    QgsVectorLayer *b = makePolygonLayer( QStringLiteral( "b" ), QStringLiteral( "b" ), 8, 100.0, 100.0 );

    QgsProcessingContext context;
    context.temporaryLayerStore()->addMapLayer( a );
    context.temporaryLayerStore()->addMapLayer( b );

    VectorMergeAlgorithm alg;
    QVariantMap params;
    params.insert( QStringLiteral( "INPUT_LAYERS" ), QVariantList{ a->id(), b->id() } );
    const QString out = tmp.filePath( QStringLiteral( "canceled.gpkg" ) );
    params.insert( QStringLiteral( "OUTPUT" ), out );

    // setProgress() is non-virtual but emits progressChanged — cancel from the
    // first reported progress onward.
    QgsProcessingFeedback feedback;
    QObject::connect( &feedback, &QgsFeedback::progressChanged, &feedback,
                      [&feedback]( double ) { feedback.cancel(); } );

    bool ok = true;
    const QVariantMap results = alg.run( params, context, &feedback, &ok );

    // Breaking out of the merge loop on cancel used to report success with a
    // partial output (#1043).
    REQUIRE_FALSE( ok );
    REQUIRE( results.isEmpty() );
    REQUIRE_FALSE( QFile::exists( out ) );
}

// ---------------------------------------------------------------------------
// vector dissolve — geometry semantics preserved, union cost not quadratic
// ---------------------------------------------------------------------------

namespace
{

void runDissolve( QgsVectorLayer *layer, const QString &outPath, bool *ok )
{
    QgsProcessingContext context;
    context.temporaryLayerStore()->addMapLayer( layer );

    VectorDissolveAlgorithm alg;
    QVariantMap params;
    params.insert( QStringLiteral( "INPUT" ), layer->id() );
    params.insert( QStringLiteral( "FIELD" ), QStringLiteral( "grp" ) );
    params.insert( QStringLiteral( "OUTPUT" ), outPath );

    QgsProcessingFeedback feedback;
    alg.run( params, context, &feedback, ok );
}

} // namespace

TEST_CASE( "vector dissolve preserves grouping and union semantics",
           "[processing-error][dissolve][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Two groups of two overlapping unit squares each: the union area per
    // group is 1.5 (two squares minus the 0.5-wide overlap) — analytic.
    QgsVectorLayer *layer = new QgsVectorLayer(
        QStringLiteral( "Polygon?crs=EPSG:4326&field=grp:string" ), QStringLiteral( "in" ),
        QStringLiteral( "memory" ) );
    QgsFeatureList features;
    const std::vector<std::pair<const char *, QgsRectangle>> specs = {
        { "a", QgsRectangle( 0.0, 0.0, 1.0, 1.0 ) },
        { "a", QgsRectangle( 0.5, 0.0, 1.5, 1.0 ) },
        { "b", QgsRectangle( 10.0, 10.0, 11.0, 11.0 ) },
        { "b", QgsRectangle( 10.5, 10.0, 11.5, 11.0 ) },
    };
    for ( const auto &spec : specs )
    {
        QgsFeature f( layer->fields() );
        f.setAttribute( 0, QString::fromLatin1( spec.first ) );
        f.setGeometry( QgsGeometry::fromRect( spec.second ) );
        features.append( f );
    }
    layer->dataProvider()->addFeatures( features );

    const QString out = tmp.filePath( QStringLiteral( "dissolved.gpkg" ) );
    bool ok = true;
    runDissolve( layer, out, &ok );
    REQUIRE( ok );
    REQUIRE( QFile::exists( out ) );

    QgsVectorLayer dissolved( out, QStringLiteral( "dis" ), QStringLiteral( "ogr" ) );
    REQUIRE( dissolved.isValid() );
    REQUIRE( dissolved.featureCount() == 2 );

    QMap<QString, double> areaByGroup;
    QgsFeatureIterator it = dissolved.getFeatures();
    QgsFeature f;
    while ( it.nextFeature( f ) )
    {
        const QString grp = f.attribute( QStringLiteral( "grp" ) ).toString();
        areaByGroup[grp] += f.geometry().area();
    }
    REQUIRE( areaByGroup.value( QStringLiteral( "a" ) ) == Approx( 1.5 ).margin( 1e-6 ) );
    REQUIRE( areaByGroup.value( QStringLiteral( "b" ) ) == Approx( 1.5 ).margin( 1e-6 ) );
}

TEST_CASE( "vector dissolve union cost stays sub-quadratic at mid scale (#1056)",
           "[processing-error][dissolve][perf]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // 1200 squares in 2 groups of 600. The old accumulate-with-combine loop
    // re-unioned the growing group geometry once per feature (O(group²) GEOS
    // work and copies); a single cascaded unaryUnion per group stays
    // near-linear.
    QgsVectorLayer *combined = new QgsVectorLayer(
        QStringLiteral( "Polygon?crs=EPSG:4326&field=grp:string" ), QStringLiteral( "combined" ),
        QStringLiteral( "memory" ) );
    QgsFeatureList all;
    {
        QgsVectorLayer *a = makePolygonLayer( QStringLiteral( "a" ), QStringLiteral( "a" ), 600, 0.0, 0.0 );
        QgsVectorLayer *b = makePolygonLayer( QStringLiteral( "b" ), QStringLiteral( "b" ), 600, 200.0, 200.0 );
        for ( QgsVectorLayer *src : { a, b } )
        {
            QgsFeatureIterator it = src->getFeatures();
            QgsFeature f;
            while ( it.nextFeature( f ) )
                all.append( f );
        }
        delete a;
        delete b;
    }
    combined->dataProvider()->addFeatures( all );

    const QString out = tmp.filePath( QStringLiteral( "dissolved_mid.gpkg" ) );
    bool ok = true;
    QElapsedTimer timer;
    timer.start();
    runDissolve( combined, out, &ok );
    const qint64 elapsedMs = timer.elapsed();

    REQUIRE( ok );
    REQUIRE( QFile::exists( out ) );
    // Generous bound (CI noise, slow hosts) but far below the minutes-scale
    // cost of the quadratic pattern this locks in (#1056).
    REQUIRE( elapsedMs < 15000 );
}

// ---------------------------------------------------------------------------
// generic CLI watchdog timeout — 64-bit, no overflow (#1043)
// ---------------------------------------------------------------------------

TEST_CASE( "generic CLI timeout_seconds stays sane: wide values clamp, never wrap negative",
           "[processing-error][generic-cli]" )
{
    const AppInit app;

    // ~24.8 days of seconds overflowed int*1000 to a negative timeout and
    // killed healthy tools instantly (#1043).
    QJsonObject wide;
    wide.insert( QStringLiteral( "timeout_seconds" ), 3000000 );
    const qint64 wideMs = GenericCliAlgorithm::toolTimeoutMs( wide );
    REQUIRE( wideMs > 0 );
    REQUIRE( wideMs <= qint64( 24 ) * 60 * 60 * 1000 );

    QJsonObject huge;
    huge.insert( QStringLiteral( "timeout_seconds" ), static_cast<qint64>( 1 ) << 40 );
    REQUIRE( GenericCliAlgorithm::toolTimeoutMs( huge ) > 0 );

    // Sensible value passes through unchanged.
    QJsonObject normal;
    normal.insert( QStringLiteral( "timeout_seconds" ), 30 );
    REQUIRE( GenericCliAlgorithm::toolTimeoutMs( normal ) == 30000 );

    // Invalid values fall back to the 30-minute default instead of 0/negative.
    QJsonObject zero;
    zero.insert( QStringLiteral( "timeout_seconds" ), 0 );
    REQUIRE( GenericCliAlgorithm::toolTimeoutMs( zero ) == qint64( 30 ) * 60 * 1000 );

    QJsonObject junk;
    junk.insert( QStringLiteral( "timeout_seconds" ), QStringLiteral( "soon" ) );
    REQUIRE( GenericCliAlgorithm::toolTimeoutMs( junk ) == qint64( 30 ) * 60 * 1000 );

    QJsonObject missing;
    REQUIRE( GenericCliAlgorithm::toolTimeoutMs( missing ) == qint64( 30 ) * 60 * 1000 );
}

// ---------------------------------------------------------------------------
// ProviderAlgorithmAdapter — cleanup and typed errors on failure paths (#1043)
// ---------------------------------------------------------------------------

namespace
{

class ThrowingStdExceptionAlgorithm : public QgsProcessingAlgorithm
{
    public:
        QString name() const override { return QStringLiteral( "error_paths_std_throw" ); }
        QString displayName() const override { return QStringLiteral( "std throw" ); }
        QString group() const override { return QStringLiteral( "test" ); }
        QString groupId() const override { return QStringLiteral( "test" ); }
        QString shortDescription() const override { return QStringLiteral( "test" ); }
        QgsProcessingAlgorithm *createInstance() const override { return new ThrowingStdExceptionAlgorithm(); }
        void initAlgorithm( const QVariantMap & ) override {}

    protected:
        QVariantMap processAlgorithm( const QVariantMap &, QgsProcessingContext &,
                                      QgsProcessingFeedback * ) override
        {
            throw std::runtime_error( "boom-not-qgs" );
        }
};

class SelfCancelingAlgorithm : public QgsProcessingAlgorithm
{
    public:
        QString name() const override { return QStringLiteral( "error_paths_self_cancel" ); }
        QString displayName() const override { return QStringLiteral( "self cancel" ); }
        QString group() const override { return QStringLiteral( "test" ); }
        QString groupId() const override { return QStringLiteral( "test" ); }
        QString shortDescription() const override { return QStringLiteral( "test" ); }
        QgsProcessingAlgorithm *createInstance() const override { return new SelfCancelingAlgorithm(); }
        void initAlgorithm( const QVariantMap & ) override {}

    protected:
        QVariantMap processAlgorithm( const QVariantMap &, QgsProcessingContext &,
                                      QgsProcessingFeedback *feedback ) override
        {
            // Simulates the external cancel bridge having fired mid-run: the
            // run ends "normally" but the feedback is canceled.
            feedback->cancel();
            return QVariantMap();
        }
};

class ErrorPathsTestProvider : public QgsProcessingProvider
{
    public:
        QString id() const override { return QStringLiteral( "test_error_paths" ); }
        QString name() const override { return QStringLiteral( "Test Error Paths" ); }
        void loadAlgorithms() override
        {
            addAlgorithm( new ThrowingStdExceptionAlgorithm() );
            addAlgorithm( new SelfCancelingAlgorithm() );
        }
};

} // namespace

TEST_CASE( "adapter reports typed Cancelled when the feedback was canceled during the run",
           "[processing-error][adapter]" )
{
    const AppInit app;
    auto *registry = QgsApplication::processingRegistry();
    REQUIRE( registry != nullptr );
    if ( !registry->providerById( QStringLiteral( "test_error_paths" ) ) )
        registry->addProvider( new ErrorPathsTestProvider() );

    const QgsProcessingAlgorithm *alg = registry->algorithmById( QStringLiteral( "test_error_paths:error_paths_self_cancel" ) );
    REQUIRE( alg != nullptr );

    sicnu::processing::ProviderAlgorithmAdapter adapter( *alg );
    bool cancelledSeen = false;
    try
    {
        adapter.execute( Json::Value(), nullptr, nullptr );
        FAIL( "expected Cancelled error" );
    }
    catch ( const sicnu::operators::RSOperatorError &e )
    {
        cancelledSeen = e.code() == sicnu::operators::ErrorCode::Cancelled;
    }
    REQUIRE( cancelledSeen );
}

TEST_CASE( "adapter propagates non-QGS std exceptions from the run after cleanup",
           "[processing-error][adapter]" )
{
    const AppInit app;
    auto *registry = QgsApplication::processingRegistry();
    REQUIRE( registry != nullptr );

    const QgsProcessingAlgorithm *alg = registry->algorithmById( QStringLiteral( "test_error_paths:error_paths_std_throw" ) );
    REQUIRE( alg != nullptr );

    sicnu::processing::ProviderAlgorithmAdapter adapter( *alg );
    bool messageSeen = false;
    try
    {
        adapter.execute( Json::Value(), nullptr, nullptr );
        FAIL( "expected runtime_error" );
    }
    catch ( const std::runtime_error &e )
    {
        messageSeen = std::string( e.what() ).find( "boom-not-qgs" ) != std::string::npos;
    }
    REQUIRE( messageSeen );
}

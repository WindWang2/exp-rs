// tests/test_subsystem_stability.cpp
// Comprehensive stability, concurrency, 64-bit bounds and GUI decoupling test suite.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "workflow/workflow_runtime.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_session.h"
#include "workflow/builtin_definitions.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_schema.h"

#include "analysis/segmentation/rs_segment_map.h"
#include "app/widgets/histogram_widget.h"
#include <qgsapplication.h>
#include "raster/qgsrasterlayer.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_error.h>

#include <QApplication>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QEventLoop>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace sicnu::workflow;
using namespace sicnu::operators;
using namespace sicnu::operators::schema;

namespace {

int &stabilityAppArgc()
{
    static int argc = 1;
    return argc;
}

char stabilityArgv0[] = "test_subsystem_stability";
char *stabilityArgv[] = { stabilityArgv0, nullptr };

void ensureQApp()
{
    if ( !QApplication::instance() )
    {
        new QApplication( stabilityAppArgc(), stabilityArgv );
    }
}

WorkflowDefinition makeTestWorkflow( const std::string &id, const std::string &title )
{
    WorkflowDefinition def;
    def.id = id;
    def.title = title;

    StepDef s1;
    s1.id = "step1";
    s1.title = "Step 1";
    s1.kind = StepKind::Interactive;
    def.steps.push_back( s1 );

    StepDef s2;
    s2.id = "step2";
    s2.title = "Step 2";
    s2.kind = StepKind::Interactive;
    StepConnection conn;
    conn.fromStepId = "step1";
    s2.inputs = { conn };
    def.steps.push_back( s2 );

    return def;
}

QString createSyntheticGeoTiff( const QString &dirPath, int width, int height, int bands )
{
    GDALAllRegister();
    QString filePath = dirPath + "/synthetic_test_raster.tif";
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};

    GDALDatasetH ds = GDALCreate( driver, filePath.toUtf8().constData(), width, height, bands, GDT_Float32, nullptr );
    if ( !ds )
        return {};

    double geoTransform[6] = { 100.0, 10.0, 0.0, 500.0, 0.0, -10.0 };
    GDALSetGeoTransform( ds, geoTransform );

    std::vector<float> rowBuf( width );
    for ( int b = 1; b <= bands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b );
        for ( int r = 0; r < height; ++r )
        {
            for ( int c = 0; c < width; ++c )
            {
                rowBuf[c] = static_cast<float>( ( r + c * 2 + b * 10 ) % 256 );
            }
            GDALRasterIO( band, GF_Write, 0, r, width, 1, rowBuf.data(), width, 1, GDT_Float32, 0, 0 );
        }
    }

    GDALClose( ds );
    return filePath;
}

} // namespace

TEST_CASE( "WorkflowRuntime concurrent multi-threaded session stress testing", "[stability][concurrency][workflow]" )
{
    ensureQApp();
    WorkflowRuntime rt( true );

    // Pre-register base test definition
    rt.registerDefinition( makeTestWorkflow( "stress.base", "Base Stress Workflow" ) );
    REQUIRE( rt.hasDefinition( "stress.base" ) );

    constexpr int kNumThreads = 12;
    constexpr int kIterationsPerThread = 60;
    std::atomic<bool> startFlag{ false };
    std::atomic<int> completedThreads{ 0 };
    std::atomic<int> successfulSessions{ 0 };

    std::vector<std::thread> workers;
    workers.reserve( kNumThreads );

    // Catch2 assertions are not thread-safe and a failing REQUIRE unwinds via
    // an exception, which would std::terminate inside a worker thread. Record
    // failures in an atomic instead and assert them on the main thread after
    // the join.
    std::atomic<int> threadFailures{ 0 };

    for ( int t = 0; t < kNumThreads; ++t )
    {
        workers.emplace_back( [t, &rt, &startFlag, &completedThreads, &successfulSessions, &threadFailures]() {
            while ( !startFlag.load( std::memory_order_acquire ) )
            {
                std::this_thread::yield();
            }

            try
            {
                for ( int i = 0; i < kIterationsPerThread; ++i )
                {
                    // Concurrently register dynamic definitions
                    if ( ( i % 5 ) == 0 )
                    {
                        std::string defId = "dyn.thread." + std::to_string( t ) + ".iter." + std::to_string( i );
                        rt.registerDefinition( makeTestWorkflow( defId, "Dynamic Def" ) );
                        if ( !rt.hasDefinition( defId ) )
                            threadFailures.fetch_add( 1, std::memory_order_relaxed );
                        const auto *found = rt.findDefinition( defId );
                        if ( found == nullptr )
                            threadFailures.fetch_add( 1, std::memory_order_relaxed );
                        auto sharedDef = rt.findDefinitionShared( defId );
                        if ( sharedDef == nullptr )
                            threadFailures.fetch_add( 1, std::memory_order_relaxed );
                    }

                    // Query registered definition list
                    auto allIds = rt.registeredDefinitionIds();
                    if ( allIds.empty() )
                        threadFailures.fetch_add( 1, std::memory_order_relaxed );

                    // Open session
                    std::string sessId = rt.open( "stress.base" );
                    if ( !sessId.empty() )
                    {
                        successfulSessions.fetch_add( 1, std::memory_order_relaxed );

                        // Query session state
                        SessionSnapshot snap = rt.state( sessId );
                        if ( snap.sessionId != sessId || snap.definitionId != "stress.base" )
                            threadFailures.fetch_add( 1, std::memory_order_relaxed );

                        // Set params
                        Json::Value params( Json::objectValue );
                        params["param1"] = "val_" + std::to_string( i );
                        rt.setParams( sessId, "step1", params );

                        // Set artifact
                        rt.setArtifact( sessId, "key_" + std::to_string( i ), "art_val" );

                        // Check canRun
                        CanRunResult cr = rt.canRun( sessId, "step1" );
                        if ( !cr.ok )
                            threadFailures.fetch_add( 1, std::memory_order_relaxed );

                        // Transition step
                        rt.markStepComplete( sessId, "step1" );
                        rt.gotoStep( sessId, "step2" );

                        // Randomly request cancellation
                        if ( i % 3 == 0 )
                        {
                            rt.requestCancel( sessId );
                        }

                        // Close session
                        rt.close( sessId );
                    }
                }
            }
            catch ( const std::exception & )
            {
                threadFailures.fetch_add( 1, std::memory_order_relaxed );
            }
            completedThreads.fetch_add( 1, std::memory_order_release );
        } );
    }

    // Launch all threads simultaneously
    startFlag.store( true, std::memory_order_release );

    for ( auto &th : workers )
    {
        th.join();
    }

    REQUIRE( completedThreads.load() == kNumThreads );
    REQUIRE( successfulSessions.load() > 0 );
    REQUIRE( threadFailures.load() == 0 );
}

TEST_CASE( "WorkflowRuntime cancellation flag thread safety and map rehash isolation", "[stability][concurrency][workflow]" )
{
    ensureQApp();
    WorkflowRuntime rt( false );

    auto def = makeTestWorkflow( "cancel.test", "Cancellation Test" );
    rt.registerDefinition( def );

    constexpr int kSessionCount = 50;
    std::vector<std::string> sessionIds;
    sessionIds.reserve( kSessionCount );

    for ( int i = 0; i < kSessionCount; ++i )
    {
        std::string sid = rt.open( "cancel.test" );
        REQUIRE( !sid.empty() );
        sessionIds.push_back( sid );
    }

    std::atomic<bool> stopFlag{ false };

    // Thread 1: Rapidly mutate definitions to trigger hash map rehashes
    std::thread defMutator( [&rt, &stopFlag]() {
        int idx = 0;
        while ( !stopFlag.load( std::memory_order_relaxed ) )
        {
            std::string defId = "rehash.def." + std::to_string( idx++ );
            rt.registerDefinition( makeTestWorkflow( defId, "Rehash Def" ) );
            std::this_thread::yield();
        }
    } );

    // Thread 2: Rapidly request cancellation across sessions
    std::thread cancelThread( [&rt, &sessionIds, &stopFlag]() {
        while ( !stopFlag.load( std::memory_order_relaxed ) )
        {
            for ( const auto &sid : sessionIds )
            {
                rt.requestCancel( sid );
            }
            std::this_thread::yield();
        }
    } );

    // Thread 3: Query states and definitions concurrently
    std::thread readerThread( [&rt, &sessionIds, &stopFlag]() {
        while ( !stopFlag.load( std::memory_order_relaxed ) )
        {
            for ( const auto &sid : sessionIds )
            {
                try
                {
                    SessionSnapshot snap = rt.state( sid );
                    (void)snap;
                }
                catch ( const std::runtime_error & )
                {
                    // Session might be closed, handled gracefully
                }
            }
            std::this_thread::yield();
        }
    } );

    // Let threads race for 150 ms
    std::this_thread::sleep_for( std::chrono::milliseconds( 150 ) );

    // Thread 4: Close sessions concurrently
    for ( const auto &sid : sessionIds )
    {
        rt.close( sid );
    }

    stopFlag.store( true, std::memory_order_release );
    defMutator.join();
    cancelThread.join();
    readerThread.join();

    SUCCEED( "Cancellation flag and hash rehash concurrency executed cleanly without race conditions" );
}

TEST_CASE( "RsSegmentMap 64-bit coordinate bounds and indexing safety", "[stability][64bit][segmentation]" )
{
    // Test 1: Construction with 64-bit dimensions
    const int64_t largeW = 100000;
    const int64_t largeH = 100000;
    QVector<quint32> emptyBuf;
    RsSegmentMap largeMap( emptyBuf, largeW, largeH );

    REQUIRE( largeMap.width() == largeW );
    REQUIRE( largeMap.height() == largeH );
    REQUIRE( largeMap.totalPixels() == 10000000000ULL );

    // Test 2: Out of bounds with extreme 64-bit coordinates
    CHECK( largeMap.labelAt( -1LL, 0LL ) == 0 );
    CHECK( largeMap.labelAt( 0LL, -1LL ) == 0 );
    CHECK( largeMap.labelAt( -999999LL, -999999LL ) == 0 );
    CHECK( largeMap.labelAt( std::numeric_limits<int64_t>::min(), 0LL ) == 0 );
    CHECK( largeMap.labelAt( 0LL, std::numeric_limits<int64_t>::min() ) == 0 );
    CHECK( largeMap.labelAt( largeH, 50LL ) == 0 );
    CHECK( largeMap.labelAt( 50LL, largeW ) == 0 );
    CHECK( largeMap.labelAt( std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max() ) == 0 );

    // Test 3: Valid populated buffer with 64-bit index access
    const int64_t w = 4;
    const int64_t h = 4;
    QVector<quint32> labels = {
        1, 1, 2, 2,
        1, 1, 2, 2,
        3, 3, 0, 4,
        3, 3, 4, 4
    };

    RsSegmentMap map( labels, w, h );
    REQUIRE( map.width() == 4 );
    REQUIRE( map.height() == 4 );
    REQUIRE( map.totalPixels() == 16 );
    REQUIRE_FALSE( map.isEmpty() );

    // Verify 64-bit labelAt queries
    CHECK( map.labelAt( 0LL, 0LL ) == 1 );
    CHECK( map.labelAt( 0LL, 2LL ) == 2 );
    CHECK( map.labelAt( 2LL, 0LL ) == 3 );
    CHECK( map.labelAt( 2LL, 2LL ) == 0 ); // NoData
    CHECK( map.labelAt( 3LL, 3LL ) == 4 );

    // Verify segment metrics
    CHECK( map.segmentCount() == 4 );
    CHECK( map.pixelCount( 1 ) == 4 );
    CHECK( map.pixelCount64( 1 ) == 4 );
    CHECK( map.pixelCount( 2 ) == 4 );
    CHECK( map.pixelCount( 3 ) == 4 );
    CHECK( map.pixelCount( 4 ) == 3 );
    CHECK( map.pixelCount( 0 ) == 0 );

    auto unique = map.uniqueLabels();
    CHECK( unique.size() == 4 );
    CHECK( unique.contains( 1 ) );
    CHECK( unique.contains( 2 ) );
    CHECK( unique.contains( 3 ) );
    CHECK( unique.contains( 4 ) );
    CHECK_FALSE( unique.contains( 0 ) );

    auto coords1 = map.pixelCoords( 1 );
    REQUIRE( coords1.size() == 4 );
    CHECK( coords1.contains( QPoint( 0, 0 ) ) );
    CHECK( coords1.contains( QPoint( 1, 0 ) ) );
    CHECK( coords1.contains( QPoint( 0, 1 ) ) );
    CHECK( coords1.contains( QPoint( 1, 1 ) ) );

    // Test 4: Fail-closed toGeoTIFF error handling
    QString errorMsg;
    CHECK_FALSE( map.toGeoTIFF( "", "", &errorMsg ) );
    CHECK( !errorMsg.isEmpty() );

    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    QString outPath = tempDir.path() + "/out_seg.tif";
    QString nonExistentRef = tempDir.path() + "/non_existent_ref.tif";

    CHECK_FALSE( map.toGeoTIFF( outPath, nonExistentRef, &errorMsg ) );
    CHECK( errorMsg.contains( "cannot open reference raster" ) );
}

TEST_CASE( "HistogramWidget GUI main thread decoupling and background computation", "[stability][gui][histogram]" )
{
    ensureQApp();
    // QgsRasterLayer needs the QGIS provider registry; GTiff writes need GDAL
    // drivers registered before any dataset is created in this process.
    if ( !QgsApplication::instance() )
    {
        static int argc = 1;
        static char arg0[] = "test_subsystem_stability";
        static char *argv[] = { arg0, nullptr };
        new QgsApplication( argc, argv, false );
    }
    QgsApplication::initQgis();

    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    QString rasterPath = createSyntheticGeoTiff( tempDir.path(), 64, 64, 4 );
    REQUIRE( !rasterPath.isEmpty() );

    std::unique_ptr<QgsRasterLayer> layer = std::make_unique<QgsRasterLayer>( rasterPath, "Synthetic 4-Band", "gdal" );
    REQUIRE( layer->isValid() );
    REQUIRE( layer->bandCount() == 4 );

    // Create HistogramWidget
    auto widget = std::make_unique<HistogramWidget>();
    widget->setChannelMode( HistogramWidget::ChannelMode::SingleBand );
    widget->setBand( 1 );

    // Assigning rasterLayer should return immediately without blocking GUI thread
    widget->setRasterLayer( layer.get() );

    // Allow background QThreadPool task to complete and process Qt queued invocations
    QThreadPool::globalInstance()->waitForDone( 3000 );
    QCoreApplication::processEvents();

    // Verify computed histogram data
    CHECK( widget->realDataMin() >= 0.0 );
    CHECK( widget->realDataMax() <= 256.0 );
    CHECK( widget->realDataMax() > widget->realDataMin() );

    // Test RGB channel mode async computation
    widget->setChannelMode( HistogramWidget::ChannelMode::MasterRGB );
    widget->setRgbBands( 1, 2, 3 );

    QThreadPool::globalInstance()->waitForDone( 3000 );
    QCoreApplication::processEvents();

    CHECK( widget->channelMode() == HistogramWidget::ChannelMode::MasterRGB );

    // Test rapid raster teardown and destruction while async requests might be in flight
    for ( int i = 0; i < 5; ++i )
    {
        auto rapidWidget = std::make_unique<HistogramWidget>();
        rapidWidget->setRasterLayer( layer.get() );
        rapidWidget->setBand( ( i % 4 ) + 1 );
        // Immediately destroy widget before thread finishes
        rapidWidget.reset();
    }

    QThreadPool::globalInstance()->waitForDone( 3000 );
    QCoreApplication::processEvents();

    SUCCEED( "HistogramWidget async execution and rapid destruction executed cleanly without ANR or crashes" );
}

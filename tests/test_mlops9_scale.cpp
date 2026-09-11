// test_mlops9_scale.cpp — Scientific MLOps 9.0 storage scale & durability
// (goal M9): 100k-run metadata stress with bounded queries, concurrent
// readers under a writer, corruption detection, and fault-injected commit
// failure (the REAL failure branch, then a clean retry).
//
// RUN_SERIAL by design: it pays 100k single-row transactions on purpose.
#include <catch2/catch_test_macros.hpp>

#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "runtime/observability/fault_registry.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

#include <atomic>
#include <thread>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

QString runIdFor( qint64 index )
{
    return QStringLiteral( "mlops9-scale-%1" ).arg( index, 7, 10, QLatin1Char( '0' ) );
}

ExperimentRun scaleRun( const QString &experimentId, qint64 index )
{
    ExperimentRun run;
    run.setRunId( runIdFor( index ) );
    run.setExperimentId( experimentId );
    run.setStatus( index % 5 == 0 ? RunStatus::Completed : RunStatus::Running );
    run.setAlgorithmId( QStringLiteral( "workflow:scale" ) );
    run.setExecutionRef( QStringLiteral( "exec-%1" ).arg( index ) );
    run.setDatasetVersionId( QStringLiteral( "ds-scale" ) );
    run.setSeed( quint64( index ) );
    run.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    return run;
}

} // namespace

TEST_CASE( "100k-run store: bounded paged access and pinned counts (M9)",
           "[mlops9][scale]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "scale9.db" ) ) ) );

    const QString experimentId = QStringLiteral( "eeeeeeee-0000-4000-8000-000000000009" );
    Experiment experiment;
    experiment.setExperimentId( experimentId );
    experiment.setName( QStringLiteral( "mlops9 scale" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    QElapsedTimer seedTimer;
    seedTimer.start();
    constexpr qint64 kRunCount = 100000;
    for ( qint64 i = 0; i < kRunCount; ++i )
    {
        const auto written = store.upsertRun( scaleRun( experimentId, i ) );
        if ( !written )
            FAIL( written.diagnostics().first().message.toStdString() );
    }
    const qint64 seedMs = seedTimer.elapsed();
    INFO( "seed_ms=" << seedMs );
    REQUIRE( store.runCount() == kRunCount );

    // Paged listing stays bounded: every page ≤ kMaxPageSize, and one page
    // fetch stays far under any interactive bound even on loaded hardware.
    QElapsedTimer pageTimer;
    pageTimer.start();
    for ( qint64 page = 0; page < 5; ++page )
    {
        const auto listing = store.listRuns( experimentId, QString(), QString(),
                                             page * ExperimentStore::kMaxPageSize );
        REQUIRE( listing.has_value() );
        REQUIRE( qint64( listing.value().second.size() ) <= ExperimentStore::kMaxPageSize );
    }
    const qint64 pageMs = pageTimer.elapsed();
    INFO( "five_pages_ms=" << pageMs );
    REQUIRE( pageMs < 5000 );

    // Filtered lookup by execution ref (cold-path scan) terminates boundedly.
    QElapsedTimer refTimer;
    refTimer.start();
    const auto refs = store.runIdsByExecutionRef( QStringLiteral( "exec-4242" ) );
    const qint64 refMs = refTimer.elapsed();
    INFO( "execution_ref_scan_ms=" << refMs );
    REQUIRE( refs.size() == 1 );
    REQUIRE( refMs < 5000 );
}

TEST_CASE( "concurrent readers coexist with a writer (M9)", "[mlops9][scale][concurrency]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "concurrent9.db" ) ) ) );

    const QString experimentId = QStringLiteral( "eeeeeeee-0000-4000-8000-0000000000c0" );
    Experiment experiment;
    experiment.setExperimentId( experimentId );
    experiment.setName( QStringLiteral( "concurrent" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    constexpr qint64 kWriterRuns = 400;
    std::atomic<bool> writerFailed{ false };
    std::atomic<bool> readersFailed{ false };
    std::atomic<bool> stopReaders{ false };

    std::thread writer( [&] {
        for ( qint64 i = 0; i < kWriterRuns; ++i )
        {
            if ( !store.upsertRun( scaleRun( experimentId, i ) ).has_value() )
                writerFailed = true;
        }
        stopReaders = true;
    } );
    std::thread reader1( [&] {
        while ( !stopReaders )
        {
            auto listing = store.listRuns( experimentId, QString(), QString(), 0 );
            if ( !listing.has_value() )
                readersFailed = true;
            std::this_thread::yield();
        }
    } );
    std::thread reader2( [&] {
        while ( !stopReaders )
        {
            // Exercises the metrics read path against the live writer.
            const auto records = store.listMetricRecords( QString(), 0, 10 );
            if ( !records.has_value() )
                readersFailed = true;
            std::this_thread::yield();
        }
    } );

    writer.join();
    reader1.join();
    reader2.join();

    REQUIRE( !writerFailed );
    REQUIRE( !readersFailed );
    REQUIRE( store.runCount() == kWriterRuns );
}

TEST_CASE( "a corrupt store file is refused, never silently misread (M9)",
           "[mlops9][scale][durability]" )
{
    QTemporaryDir dir;
    const QString path = dir.filePath( QStringLiteral( "corrupt9.db" ) );
    {
        ExperimentStore store;
        REQUIRE( store.open( path ) );
        const QString experimentId = QStringLiteral( "eeeeeeee-0000-4000-8000-00000000000d" );
        Experiment experiment;
        experiment.setExperimentId( experimentId );
        experiment.setName( QStringLiteral( "corrupt" ) );
        REQUIRE( store.upsertExperiment( experiment ).has_value() );
    }

    // Overwrite the file with text garbage (truncating the SQLite header and
    // every page).
    QFile garbage( path );
    REQUIRE( garbage.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    garbage.write( "this is not a sqlite database, this is text filler 0123456789" );
    garbage.close();

    ExperimentStore store;
    QString error;
    CHECK( !store.open( path, &error ) );
    CHECK( !error.isEmpty() );
}

TEST_CASE( "an injected commit fault takes the real failure branch, then recovers (M9)",
           "[mlops9][scale][durability]" )
{
    using namespace sicnu::runtime::observability::fault;

    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "fault9.db" ) ) ) );

    const QString experimentId = QStringLiteral( "eeeeeeee-0000-4000-8000-00000000000f" );
    Experiment experiment;
    experiment.setExperimentId( experimentId );
    experiment.setName( QStringLiteral( "fault" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    armFault( FaultAction{ "experiment_store.commit", Mode::Always, 0, {} } );
    const auto refused = store.upsertRun( scaleRun( experimentId, 0 ) );
    disarmFault( "experiment_store.commit" );
    REQUIRE( !refused.has_value() );
    // The store is still fully usable: the failed transaction left no half-run.
    REQUIRE( store.runCount() == 0 );

    const auto recovered = store.upsertRun( scaleRun( experimentId, 0 ) );
    REQUIRE( recovered.has_value() );
    REQUIRE( store.runCount() == 1 );
    const auto readBack = store.runById( runIdFor( 0 ) );
    REQUIRE( readBack.has_value() );
    CHECK( readBack->status() == scaleRun( experimentId, 0 ).status() );
}

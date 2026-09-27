// test_mlops9_scale.cpp — Scientific MLOps 9.0 storage scale & durability
// (goal M9): 100k-run metadata stress with bounded queries, concurrent
// readers under a writer, corruption detection, and fault-injected commit
// failure (the REAL failure branch, then a clean retry).
//
// RUN_SERIAL by design: it pays 100k single-row transactions on purpose.
//
// #1363 scale budget: the full 100k seed is the product SLO shape and stays
// the default on Release/RelWithDebInfo profiles. Debug (+sanitizer) builds
// pay ~3x instrumentation cost per transaction and blew the wall-time
// budget, so their default is 30k — the SAME linear-cost contract at reduced
// n (the per-run budgets below scale with the active run count). A LONG_RUN
// session can restore full scale in a Debug tree without a rebuild:
//   SICNU_MLOPS9_SCALE_RUNS=100000 ctest -R mlops9_scale_longrun -L LONG_RUN
#include <catch2/catch_test_macros.hpp>

#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "runtime/observability/fault_registry.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

#include <atomic>
#include <cstdio>
#include <optional>
#include <thread>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

#ifdef SICNU_SCALE_TEST_DEFAULT_RUNS
constexpr qint64 kDefaultScaleRuns = SICNU_SCALE_TEST_DEFAULT_RUNS;
#else
constexpr qint64 kDefaultScaleRuns = 100000;
#endif

// Ref-scan budget per run (the bounded-per-run ⇒ linear-total contract).
// Baseline measured ~0.7 ms/run at 100k on the baseline host under three
// concurrent track builds; 1.2 ms/run keeps that headroom. Sanitizer
// instrumentation roughly doubles the per-row SQLite cost (see
// test_workspace_catalog.cpp), so the sanitizer tier gets the doubled bound.
double refScanBudgetPerRunMs()
{
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
    return 2.4;
#else
    return 1.2;
#endif
}

// Active run count for this process: the compile-time profile default, or a
// clamped SICNU_MLOPS9_SCALE_RUNS override (LONG_RUN sessions only; the
// variable is read by this test process and nothing else).
qint64 activeScaleRuns()
{
    qint64 runs = kDefaultScaleRuns;
    const QString env = qEnvironmentVariable( "SICNU_MLOPS9_SCALE_RUNS" );
    if ( !env.isEmpty() )
    {
        bool ok = false;
        const qint64 parsed = env.toLongLong( &ok );
        if ( ok && parsed >= 1 && parsed <= 1000000 )
            runs = parsed;
    }
    return runs;
}

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
    const QDateTime start = QDateTime::currentDateTimeUtc();
    run.setCreatedAtUtc( start );
    if ( run.status() == RunStatus::Completed )
    {
        // Terminal runs must carry their finish time (store read contract).
        run.setStartedAtUtc( start );
        run.setFinishedAtUtc( start.addMSecs( 1 ) );
    }
    return run;
}

} // namespace

TEST_CASE( "100k-run store: bounded paged access and pinned counts (M9)",
           "[mlops9][scale][longrun]" )
{
    const qint64 kRunCount = activeScaleRuns();
    INFO( "active_scale_runs=" << kRunCount
          << " (default=" << kDefaultScaleRuns
          << "; override via SICNU_MLOPS9_SCALE_RUNS)" );
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
    for ( qint64 i = 0; i < kRunCount; ++i )
    {
        const auto written = store.upsertRun( scaleRun( experimentId, i ) );
        if ( !written )
            FAIL( written.diagnostics().first().message.toStdString() );
    }
    const qint64 seedMs = seedTimer.elapsed();
    INFO( "seed_ms=" << seedMs << " seed_ms_per_run=" << ( double( seedMs ) / double( kRunCount ) ) );
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

    // Filtered lookup by execution ref (cold-path SUBSTRING scan over the
    // run JSON) terminates boundedly. The needle is derived from the ACTIVE
    // scale (index kRunCount-1, the highest ref with the most digits) so any
    // scale hits exactly one run: no sibling ref can contain it as a
    // substring, because that would require an index >= kRunCount (the old
    // hard-coded exec-99999 broke the 30k Debug default — found by the
    // focused ctest run).
    QElapsedTimer refTimer;
    refTimer.start();
    const auto refLookup = store.runIdsByExecutionRef(
        QStringLiteral( "exec-%1" ).arg( kRunCount - 1 ) );
    REQUIRE( refLookup.has_value() );
    const QStringList refs = refLookup.value();
    const qint64 refMs = refTimer.elapsed();
    INFO( "execution_ref_scan_ms=" << refMs );
    REQUIRE( refs.size() == 1 );
    // The ref scan is the documented COLD-PATH reconciliation helper (a
    // bounded substring scan over every run JSON — the store docstring tells
    // live callers to keep their own ref→runId map). The 9.0 contract
    // requires BOUNDED and LINEAR: the budget is expressed PER RUN so the
    // 30k Debug default and a full-scale LONG_RUN session assert the same
    // complexity contract (see refScanBudgetPerRunMs for the derivation).
    const double refBudgetMs = refScanBudgetPerRunMs() * double( kRunCount );
    INFO( "execution_ref_budget_ms=" << refBudgetMs );
    REQUIRE( double( refMs ) < refBudgetMs );
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
            {
                fprintf( stderr, "reader1 diag: %s\n",
                         listing.diagnostics().first().message.toUtf8().constData() );
                readersFailed = true;
            }
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

    // RAII arming: the fault can never leak into a later case on a failure
    // path (review round 1). Inner scope: the fault is disarmed before the
    // recovery write below.
    std::optional<sicnu::data::Result<void>> refused;
    {
        const ArmedFault guard{
            FaultAction{ "experiment_store.commit", Mode::Always, 0, {} } };
        refused = store.upsertRun( scaleRun( experimentId, 0 ) );
    }
    REQUIRE( !refused.value().has_value() );
    // The store is still fully usable: the failed transaction left no half-run.
    REQUIRE( store.runCount() == 0 );

    const auto recovered = store.upsertRun( scaleRun( experimentId, 0 ) );
    REQUIRE( recovered.has_value() );
    REQUIRE( store.runCount() == 1 );
    const auto readBack = store.runById( runIdFor( 0 ) );
    REQUIRE( readBack.has_value() );
    CHECK( readBack->status() == scaleRun( experimentId, 0 ).status() );
}

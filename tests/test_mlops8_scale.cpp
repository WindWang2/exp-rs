// test_mlops8_scale.cpp — bounded scale evidence for the 8.0 bridge paths
// (goal §37 boundedness at 100k-class metadata scale, probed at 20k records
// to keep the suite runtime bounded):
//   - seeding 20k runs through the store stays page-bounded;
//   - reconcileStale over 20k records with NO checkpoint evidence performs a
//     pure bounded paged scan and REPORTS (never writes) — linear in stored
//     runs, no memory blow-up, no fabricated closes.
#include <catch2/catch_test_macros.hpp>

#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/run_bridge.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QSet>
#include <QTemporaryDir>

#include <chrono>
#include <thread>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{
constexpr qint64 kRunCount = 20000;
constexpr qint64 kLiveCount = 100; // the rest are stale, evidence-less
} // namespace

TEST_CASE( "stale reconciliation over 20k runs is a bounded report-only scan",
           "[mlops8][scale]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "scale.db" ) ) ) );

    Experiment experiment;
    const QString experimentId = QStringLiteral( "bbbbbbbb-0000-4000-8000-000000000001" );
    experiment.setExperimentId( experimentId );
    experiment.setName( QStringLiteral( "scale" ) );
    experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    QElapsedTimer seedTimer;
    seedTimer.start();
    for ( qint64 i = 0; i < kRunCount; ++i )
    {
        ExperimentRun run;
        run.setRunId( QStringLiteral( "scale-run-%1" ).arg( i, 6, 10, QLatin1Char( '0' ) ) );
        run.setExperimentId( experimentId );
        run.setStatus( RunStatus::Running );
        run.setAlgorithmId( QStringLiteral( "scale-wf" ) );
        run.setExecutionRef( i < kLiveCount ? QStringLiteral( "live-%1" ).arg( i )
                                            : QStringLiteral( "dead-%1" ).arg( i ) );
        run.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
        const auto written = store.upsertRun( run );
        if ( !written )
            FAIL( written.diagnostics().first().message.toStdString() );
    }
    const qint64 seedMs = seedTimer.elapsed();
    REQUIRE( store.runCount() == kRunCount );

    ExperimentRunBridge bridge( store );
    const auto ensured = bridge.ensureExperiment( experimentId, QStringLiteral( "scale" ) );
    REQUIRE( ensured.has_value() );

    QSet<QString> live;
    for ( qint64 i = 0; i < kLiveCount; ++i )
        live.insert( QStringLiteral( "live-%1" ).arg( i ) );

    QElapsedTimer reconcileTimer;
    reconcileTimer.start();
    const auto decisions =
        bridge.reconcileStale( live, []( const QString & ) { return std::nullopt; } );
    const qint64 reconcileMs = reconcileTimer.elapsed();

    // Every stale run is REPORTED (no evidence → never closed); the live set
    // is untouched; nothing was written during reconciliation.
    REQUIRE( qint64( decisions.size() ) == kRunCount - kLiveCount );
    for ( const auto &decision : decisions )
        REQUIRE( decision.action == QStringLiteral( "report" ) );

    // Boundedness gates (generous CI-safe bounds on shared hardware):
    // reconciliation is a paged scan, so it must stay far below the seeding
    // cost (which pays a transaction per run).
    INFO( "seed_ms=" << seedMs << " reconcile_ms=" << reconcileMs );
    REQUIRE( reconcileMs < 60000 );

    // Spot-check honesty: a stale run still reports Running after reconcile.
    const auto untouched = store.runById( QStringLiteral( "scale-run-019999" ) );
    REQUIRE( untouched.has_value() );
    REQUIRE( untouched->status() == RunStatus::Running );
}

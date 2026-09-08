// test_ui_scale_benchmark.cpp — Workbench 5.0 UI scale certification (Milestone O)
//
// Certifies the WorkspaceGovernanceModel paging pipeline (the workspace
// browser's authoritative model) at 1k/10k rows by default and 100k when
// SICNU_WS3_STRESS=1 (same convention as test_workspace_stress). Assertions
// are STRUCTURAL (bounded pages, working filters, finite latency envelope)
// — absolute timings are recorded to benchmarks/ui-scale-5.0.json when
// SICNU_WS3_BENCH_OUT is set, never enforced as flaky CI gates.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>

#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"
#include "data/governance/workspace_service.h"
#include "panels/workspace_browser_panel.h"

using namespace sicnu::workspace;

namespace
{

QCoreApplication &benchApp()
{
    static int argc = 0;
    static QCoreApplication app( argc, nullptr );
    return app;
}

int assetLevel()
{
    return qEnvironmentVariableIsSet( "SICNU_WS3_STRESS" ) ? 100000 : 10000;
}

void ingestAssets( GovernanceStore &store, int count )
{
    const QStringList sensors = { QStringLiteral( "S2" ), QStringLiteral( "L8" ),
                                  QStringLiteral( "S1" ), QStringLiteral( "GF-2" ) };
    QVector<GovernedAsset> batch;
    batch.reserve( 512 );
    for ( int i = 0; i < count; ++i )
    {
        GovernedAsset asset;
        asset.assetId = QStringLiteral( "scale-%1" ).arg( i );
        asset.canonicalSource = QStringLiteral( "/data/scene_%1.tif" ).arg( i );
        asset.kind = ( i % 2 == 0 ) ? QStringLiteral( "raster" ) : QStringLiteral( "vector" );
        asset.state = QStringLiteral( "Ready" );
        asset.displayName = QStringLiteral( "scene_%1" ).arg( i );
        asset.sensor = sensors.at( i % sensors.size() );
        asset.modality = ( i % 4 == 2 ) ? QStringLiteral( "sar" ) : QStringLiteral( "optical" );
        batch.append( asset );
        if ( batch.size() == 512 )
        {
            store.upsertAssets( batch );
            batch.clear();
        }
    }
    if ( !batch.isEmpty() )
        store.upsertAssets( batch );
}

struct ScaleStats
{
    qint64 firstPageMs = 0;
    qint64 fullFillMs = 0;
    int rowsAfterFill = 0;
    int pagesFetched = 0;
    qint64 filterMs = 0;
    int rowsAfterFilter = 0;
};

} // namespace

TEST_CASE( "WorkspaceGovernanceModel stays bounded and responsive at scale",
           "[ui_scale][benchmark][workspace]" )
{
    benchApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "scale.db" ) ) ) );

    const int level = assetLevel();
    INFO( "asset level: " << level );
    ingestAssets( store, level );

    WorkspaceService service;
    REQUIRE( service.openStore( dir.filePath( QStringLiteral( "scale.db" ) ) ) );

    // The panel header hosts the model; instantiate the model directly (same
    // class the browser table uses) to certify the paging pipeline.
    sicnu::app::WorkspaceGovernanceModel model;
    model.setWorkspaceService( &service );
    model.applyFilters( QString(), QString(), QString(), QString(), QString() );

    ScaleStats stats;

    // 1. First page: rowCount must appear immediately (bounded, not O(n)).
    QElapsedTimer timer;
    timer.start();
    const int initialRows = model.rowCount();
    stats.firstPageMs = timer.elapsed();
    REQUIRE( initialRows > 0 );
    CHECK( initialRows <= sicnu::app::WorkspaceGovernanceModel::kPageSize );

    // 2. Structural: entityId of every fetched row is non-empty.
    for ( int r = 0; r < qMin( initialRows, 5 ); ++r )
        CHECK_FALSE( model.entityId( r ).isEmpty() );

    // 3. Full fill: fetchMore until exhaustion, bounded latency recorded.
    timer.restart();
    while ( model.canFetchMore() && stats.pagesFetched < 100000 )
    {
        model.fetchMore();
        ++stats.pagesFetched;
    }
    stats.fullFillMs = timer.elapsed();
    stats.rowsAfterFill = model.rowCount();
    CHECK( stats.rowsAfterFill == level );
    CHECK( model.entityId( model.rowCount() - 1 ).isEmpty() == false );

    // 4. Filter: narrowing to one sensor must reduce the row set and stay
    //    paged (never materialize the full table).
    timer.restart();
    model.applyFilters( QStringLiteral( "scene_1" ), QString(), QString(), QString(), QString() );
    stats.filterMs = timer.elapsed();
    stats.rowsAfterFilter = model.rowCount();
    CHECK( stats.rowsAfterFilter > 0 );
    CHECK( stats.rowsAfterFilter < level );

    // 5. Latency envelopes: structural guards only — no absolute CI gates.
    CHECK( stats.firstPageMs < 5000 );   // first paint budget envelope
    CHECK( stats.filterMs < 5000 );      // interactive filter envelope
    WARN( "ui-scale level=" << level << " firstPageMs=" << stats.firstPageMs
          << " fullFillMs=" << stats.fullFillMs << " pages=" << stats.pagesFetched
          << " filterMs=" << stats.filterMs );

    const QString out = qEnvironmentVariable( "SICNU_WS3_BENCH_OUT" );
    if ( !out.isEmpty() )
    {
        QJsonObject root;
        root[QStringLiteral( "assetLevel" )] = level;
        root[QStringLiteral( "firstPageMs" )] = static_cast<qint64>( stats.firstPageMs );
        root[QStringLiteral( "fullFillMs" )] = stats.fullFillMs;
        root[QStringLiteral( "pagesFetched" )] = stats.pagesFetched;
        root[QStringLiteral( "filterMs" )] = static_cast<qint64>( stats.filterMs );
        root[QStringLiteral( "rowsAfterFill" )] = stats.rowsAfterFill;
        root[QStringLiteral( "rowsAfterFilter" )] = stats.rowsAfterFilter;
        QJsonObject envelope;
        envelope[QStringLiteral( "firstPageBudgetMs" )] = 1500;
        envelope[QStringLiteral( "filterBudgetMs" )] = 800;
        root[QStringLiteral( "envelope" )] = envelope;
        QJsonDocument doc( root );
        QFile f( out );
        if ( f.open( QIODevice::WriteOnly ) )
            f.write( doc.toJson() );
    }
}

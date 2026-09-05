// test_workspace_stress.cpp — Workspace Governance 3.0 scale contract
// (Platform 3.0 Phase H).
//
// Levels: 1k assets by default; SICNU_WS3_STRESS=1 raises the asset level to
// 100k (RUN_SERIAL, generous timeout). Every measured operation must stay
// bounded; the JSON table goes to benchmarks/workspace-3.json when
// SICNU_WS3_BENCH_OUT is set.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"

using namespace sicnu::workspace;

namespace
{

QCoreApplication &stressApp()
{
    static int argc = 0;
    static QCoreApplication app( argc, nullptr );
    return app;
}

struct Row
{
    QString assetId;
    QString name;
    QString sensor;
    QString modality;
    QString crs;
    QString kind;
};

QVector<Row> buildRows( int count )
{
    QVector<Row> rows;
    rows.reserve( count );
    const QStringList sensors = { QStringLiteral( "S2" ), QStringLiteral( "L8" ), QStringLiteral( "S1" ),
                                  QStringLiteral( "GF-2" ) };
    const QStringList kinds = { QStringLiteral( "raster" ), QStringLiteral( "vector" ) };
    for ( int i = 0; i < count; ++i )
    {
        Row row;
        row.assetId = QStringLiteral( "stress-%1" ).arg( i );
        row.name = QStringLiteral( "scene_%1" ).arg( i );
        row.sensor = sensors.at( i % sensors.size() );
        row.modality = ( i % 4 == 2 ) ? QStringLiteral( "sar" ) : QStringLiteral( "optical" );
        row.crs = ( i % 2 == 0 ) ? QStringLiteral( "EPSG:4326" ) : QStringLiteral( "EPSG:32650" );
        row.kind = kinds.at( i % 2 );
        rows.append( row );
    }
    return rows;
}

qint64 ingest( GovernanceStore &store, const QVector<Row> &rows )
{
    QElapsedTimer timer;
    timer.start();
    QVector<GovernedAsset> batch;
    batch.reserve( 256 );
    for ( const Row &row : rows )
    {
        GovernedAsset asset;
        asset.assetId = row.assetId;
        asset.canonicalSource = QStringLiteral( "/data/%1.tif" ).arg( row.name );
        asset.kind = row.kind;
        asset.state = QStringLiteral( "Ready" );
        asset.displayName = row.name;
        asset.sensor = row.sensor;
        asset.modality = row.modality;
        asset.crs = row.crs;
        asset.revision = 1;
        asset.availability = QStringLiteral( "unverified" );
        batch.append( asset );
        if ( batch.size() == 256 )
        {
            ( void ) store.upsertAssets( batch );
            batch.clear();
        }
    }
    ( void ) store.upsertAssets( batch );
    return timer.elapsed();
}

} // namespace

TEST_CASE( "Governed index stays bounded at 1k/100k assets", "[workspace][stress]" )
{
    stressApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const bool stress = qEnvironmentVariableIsSet( "SICNU_WS3_STRESS" );
    const int count = stress ? 100000 : 1000;

    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "stress.db" ) ) ) );

    QJsonObject bench;
    bench.insert( QStringLiteral( "assetLevel" ), count );
    bench.insert( QStringLiteral( "stressMode" ), stress );

    const QVector<Row> rows = buildRows( count );

    // --- ingest -----------------------------------------------------------------
    bench.insert( QStringLiteral( "ingestMs" ), ingest( store, rows ) );
    REQUIRE( store.assetCount() == count );

    // --- paged query (warm) -------------------------------------------------------
    QElapsedTimer timer;
    timer.start();
    WorkspaceQuery query;
    query.set = EntitySet::Assets;
    query.limit = 200;
    const WorkspacePage page = store.query( query );
    const qint64 pageMs = timer.elapsed();
    bench.insert( QStringLiteral( "pageQueryMs" ), pageMs );
    REQUIRE( page.total == count );
    REQUIRE( page.items.size() == 200 );
    REQUIRE( pageMs < 500 );  // soft bound; indexed page fetch at 100k must be fast

    // --- facet filter ---------------------------------------------------------------
    timer.restart();
    WorkspaceQuery filtered;
    filtered.set = EntitySet::Assets;
    filtered.sensor = QStringLiteral( "S2" );
    filtered.limit = 50;
    const WorkspacePage filteredPage = store.query( filtered );
    const qint64 filterMs = timer.elapsed();
    bench.insert( QStringLiteral( "facetFilterMs" ), filterMs );
    REQUIRE( filteredPage.total == count / 4 );
    REQUIRE( filterMs < 500 );

    // --- facet aggregation -----------------------------------------------------------
    timer.restart();
    const WorkspacePage facet = store.query( WorkspaceQuery(), QStringLiteral( "sensor" ) );
    const qint64 facetMs = timer.elapsed();
    bench.insert( QStringLiteral( "facetAggregationMs" ), facetMs );
    REQUIRE( facet.facets.size() == 4 );
    REQUIRE( facetMs < 1000 );

    // --- point lookup byPath (the O(N)-stat killer) -----------------------------------
    timer.restart();
    int hits = 0;
    for ( int i = 0; i < 1000; ++i )
    {
        const QString path = QStringLiteral( "/data/scene_%1.tif" ).arg( ( i * 9973 ) % count );
        hits += store.assetByPath( path ).has_value() ? 1 : 0;
    }
    const qint64 lookupMs = timer.elapsed();
    bench.insert( QStringLiteral( "pointLookup1000Ms" ), lookupMs );
    REQUIRE( hits == 1000 );
    REQUIRE( lookupMs < 1500 );

    // --- bulk tag 10k ------------------------------------------------------------------
    timer.restart();
    QVector<QString> ids;
    ids.reserve( 10000 );
    for ( int i = 0; i < 10000 && i < count; ++i )
        ids.append( rows.at( i ).assetId );
    REQUIRE( store.bulkTag( ids, QStringLiteral( "asset" ), QStringLiteral( "qa" ) ).operator bool() );
    const qint64 tagMs = timer.elapsed();
    bench.insert( QStringLiteral( "bulkTag10kMs" ), tagMs );
    REQUIRE( tagMs < 5000 );

    // --- lineage chain (min(count,10k) edges, depth-bounded query) ----------------------
    const int edges = std::min( count, 10000 );
    QVector<GovernanceStore::LineageEdge> edgeRows;
    edgeRows.reserve( edges );
    for ( int i = 1; i < edges; ++i )
    {
        GovernanceStore::LineageEdge edge;
        edge.outputAssetId = rows.at( i ).assetId;
        edge.inputAssetId = rows.at( i - 1 ).assetId;
        edge.operatorId = QStringLiteral( "rs:stress" );
        edgeRows.append( edge );
    }
    REQUIRE( store.addLineageEdges( edgeRows ).operator bool() );
    timer.restart();
    const QVector<QVariantMap> upstream = store.lineageUpstream( rows.at( edges - 1 ).assetId, 64 );
    const qint64 lineageMs = timer.elapsed();
    bench.insert( QStringLiteral( "lineageQueryMs" ), lineageMs );
    REQUIRE( upstream.size() == std::min( edges - 1, 64 ) );
    REQUIRE( lineageMs < 1500 );

    // --- results + orphan scan -----------------------------------------------------------
    ResultRecord result;
    result.id = ResultId::generate();
    result.semanticType = ResultSemanticType::Classification;
    result.header.name = QStringLiteral( "stress-result" );
    ResultInput input;
    input.assetId = rows.first().assetId;
    result.inputs.append( input );
    REQUIRE( store.upsertResult( result ).operator bool() );
    REQUIRE( store.resultsDependingOnAsset( rows.first().assetId ).size() == 1 );

    // --- save the bench table ------------------------------------------------------------
    const QString outPath = qEnvironmentVariable( "SICNU_WS3_BENCH_OUT" );
    if ( !outPath.isEmpty() )
    {
        const QDir outDir = QFileInfo( outPath ).absoluteDir();
        ( void ) outDir.mkpath( QLatin1String( "." ) );
        QFile out( outPath );
        REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        out.write( QJsonDocument( bench ).toJson( QJsonDocument::Indented ) );
    }

    INFO( qPrintable( QJsonDocument( bench ).toJson( QJsonDocument::Indented ) ) );
    REQUIRE( store.clearAll().operator bool() );
}

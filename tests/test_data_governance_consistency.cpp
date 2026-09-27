// test_data_governance_consistency.cpp — Track 13 R4 WP-F (narrowed):
// governance mirror reference consistency.
//
// Measured premise correction (see PLAN.md): src/data does NOT reference
// dataset-version fingerprints anywhere (zero includes of dataset headers in
// src/data). The in-scope consistency domain is the governance mirror's own
// reference families. This file proves, with existing read primitives only:
//   - every registered reference family (result_input, run_output, lineage
//     edge) yields a walkable asset→entity chain;
//   - planted dangling rows are SURFACED by the reference enumeration and
//     detectable by crossing it with entity existence — with the dangling
//     path (family) and field (entity id) in the report;
//   - the fingerprint lookup convention (platform digests are lowercase
//     hex) holds for the relink path.
//
// Design note: the batch mirror paths (addRunOutputs / addLineageEdges)
// intentionally do not require endpoint existence — bulk mirror imports may
// legitimately stage edges before their endpoint rows (mirror semantics).
// Consistency is therefore a REPORTING concern, proven here, not a write
// gate (DECISIONS.md, exemption E4).
#include <catch2/catch_test_macros.hpp>

#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"

#include <QDateTime>
#include <QTemporaryDir>

using namespace sicnu::workspace;

namespace
{

GovernedAsset makeAsset( const QString &id )
{
    GovernedAsset asset;
    asset.assetId = id;
    asset.canonicalSource = QStringLiteral( "/data/%1.tif" ).arg( id );
    asset.kind = QStringLiteral( "raster" );
    asset.state = QStringLiteral( "Ready" );
    asset.persistence = QStringLiteral( "project" );
    asset.displayName = id;
    asset.acquisitionMs = QDateTime::currentMSecsSinceEpoch();
    asset.revision = 1;
    asset.availability = QStringLiteral( "unverified" );
    return asset;
}

/// A dangling-reference report row: the path (reference family) and the
/// field (entity id) are machine-readable, per the R7 locator contract.
struct DanglingRef
{
    QString family;
    QString entityId;
    QString detail;
};

} // namespace

TEST_CASE( "every planted reference family yields a walkable asset chain",
           "[data][governance][consistency][positive]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    const QString sourceId = QStringLiteral( "asset-source" );
    const QString consumerId = QStringLiteral( "asset-consumer" );
    REQUIRE( store.upsertAsset( makeAsset( sourceId ) ).operator bool() );
    REQUIRE( store.upsertAsset( makeAsset( consumerId ) ).operator bool() );

    // result_input family: a result consuming the source asset.
    ResultRecord result;
    result.id = ResultId::generate();
    result.semanticType = ResultSemanticType::Classification;
    result.header.name = QStringLiteral( "classification-1" );
    ResultInput input;
    input.assetId = sourceId;
    input.revision = 2;
    result.inputs.append( input );
    REQUIRE( store.upsertResult( result ).operator bool() );

    // run_output family: a run producing the source asset.
    RunRecord run;
    run.id = QStringLiteral( "run-1" );
    run.workflowId = QStringLiteral( "wf-x" );
    run.state = QStringLiteral( "Completed" );
    run.outputAssetIds.append( sourceId );
    REQUIRE( store.upsertRun( run ).operator bool() );
    REQUIRE( store.linkRunOutput( QStringLiteral( "run-1" ), sourceId ).operator bool() );

    // lineage family: consumer derives from the source asset.
    GovernanceStore::LineageEdge edge;
    edge.outputAssetId = consumerId;
    edge.inputAssetId = sourceId;
    edge.operatorId = QStringLiteral( "op.ndvi" );
    edge.runId = QStringLiteral( "run-1" );
    REQUIRE( store.addLineageEdges( { edge } ).operator bool() );

    // The walk: every reference of the source asset resolves to a live
    // entity (asset → references → entities).
    const QVector<GovernanceStore::AssetReference> references =
        store.collectAssetReferences( sourceId );
    REQUIRE( references.size() == 3 );
    for ( const GovernanceStore::AssetReference &reference : references )
    {
        if ( reference.kind == QStringLiteral( "result_input" ) )
            CHECK( store.resultById( reference.entityId ).has_value() );
        else if ( reference.kind == QStringLiteral( "run_output" ) )
            CHECK( store.runById( reference.entityId ).has_value() );
        else if ( reference.kind == QStringLiteral( "lineage_downstream" ) )
            CHECK( store.assetById( reference.entityId ).has_value() );
        else
            FAIL( "unexpected reference family" );
    }

    // Traversals stay on live nodes and expose the edge operator.
    CHECK( store.lineageUpstream( consumerId ).size() == 1 );
    CHECK( store.lineageDownstream( sourceId ).size() == 1 );
    CHECK( store.directEdges( consumerId, true ).size() == 1 );

    // Fingerprint lookup: platform digests are lowercase hex; the relink
    // path finds the asset by digest.
    GovernedAsset fingerprinted = makeAsset( QStringLiteral( "asset-dup" ) );
    fingerprinted.contentFingerprint =
        QStringLiteral( "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
    REQUIRE( store.upsertAsset( fingerprinted ).operator bool() );
    CHECK( store.assetsByFingerprint(
               QStringLiteral( "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ) )
               .size() == 1 );
}

TEST_CASE( "planted dangling references are surfaced with family and entity id",
           "[data][governance][consistency][dangling]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    const QString liveAsset = QStringLiteral( "asset-edge-output" );
    REQUIRE( store.upsertAsset( makeAsset( liveAsset ) ).operator bool() );

    // Plant dangling rows through the mirror's batch paths (allowed by
    // design — bulk staging may precede endpoint rows):
    //   run_output → run "run-ghost" that does not exist;
    //   lineage input → asset "asset-ghost" that does not exist.
    RunRecord run;
    run.id = QStringLiteral( "run-staging" );
    run.workflowId = QStringLiteral( "wf-x" );
    run.state = QStringLiteral( "Completed" );
    REQUIRE( store.upsertRun( run ).operator bool() );
    QVector<QPair<QString, QString>> outputs;
    outputs.append( { QStringLiteral( "run-ghost" ), liveAsset } );
    outputs.append( { QStringLiteral( "run-staging" ), liveAsset } );
    REQUIRE( store.addRunOutputs( outputs ).operator bool() );

    GovernanceStore::LineageEdge ghost;
    ghost.outputAssetId = liveAsset;
    ghost.inputAssetId = QStringLiteral( "asset-ghost" );
    ghost.operatorId = QStringLiteral( "op.ghost" );
    REQUIRE( store.addLineageEdges( { ghost } ).operator bool() );

    // The consistency check composed from existing read primitives: every
    // reference row must point at an entity the store knows.
    QVector<DanglingRef> dangling;
    for ( const QString &assetId : store.assetIds() )
    {
        for ( const GovernanceStore::AssetReference &reference :
              store.collectAssetReferences( assetId ) )
        {
            bool resolved = false;
            if ( reference.kind == QStringLiteral( "result_input" ) )
                resolved = store.resultById( reference.entityId ).has_value();
            else if ( reference.kind == QStringLiteral( "run_output" ) )
                resolved = store.runById( reference.entityId ).has_value();
            else if ( reference.kind == QStringLiteral( "dataset_member" ) )
                resolved = store.datasetById( reference.entityId ).has_value();
            else if ( reference.kind == QStringLiteral( "lineage_downstream" ) )
                resolved = store.assetById( reference.entityId ).has_value();
            if ( !resolved )
                dangling.append( { reference.kind, reference.entityId, reference.detail } );
        }
    }

    REQUIRE( dangling.size() == 1 );
    CHECK( dangling.first().family == QStringLiteral( "run_output" ) );
    CHECK( dangling.first().entityId == QStringLiteral( "run-ghost" ) );

    // The lineage ghost is surfaced by direct edge inspection (input side
    // carries no reference row for the ghost itself — the EDGE is attached
    // to the live output), so the checker also walks directEdges:
    const QVector<GovernanceStore::LineageEdge> edges =
        store.directEdges( liveAsset, true );
    REQUIRE( edges.size() == 1 );
    CHECK( store.assetById( edges.first().inputAssetId ).has_value() == false );
    CHECK( edges.first().operatorId == QStringLiteral( "op.ghost" ) );

    // Traversal exposes the ghost node (never silently dropped): a
    // downstream consumer of the report sees the broken edge's endpoint.
    const QVariantMap upstream = store.lineageUpstream( liveAsset ).value( 0 );
    CHECK( upstream.value( QStringLiteral( "assetId" ) ).toString() ==
           QStringLiteral( "asset-ghost" ) );
}

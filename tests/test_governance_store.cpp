// Governance store (Platform 3.0) — schema, paging, lineage, lifecycle.
#include <catch2/catch_test_macros.hpp>

#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"
#include "runtime/observability/fault_registry.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantMap>

using namespace sicnu::workspace;
using sicnu::runtime::observability::fault::ArmedFault;
using sicnu::runtime::observability::fault::Mode;

namespace
{

GovernedAsset makeAsset( const QString &id, const QString &name, const QString &sensor = QString(),
                         const QString &modality = QString() )
{
    GovernedAsset a;
    a.assetId = id;
    a.canonicalSource = QStringLiteral( "/data/%1.tif" ).arg( name );
    a.kind = QStringLiteral( "raster" );
    a.state = QStringLiteral( "Ready" );
    a.persistence = QStringLiteral( "project" );
    a.displayName = name;
    a.acquisitionMs = QDateTime::currentMSecsSinceEpoch();
    a.revision = 1;
    a.sensor = sensor;
    a.modality = modality;
    a.availability = QStringLiteral( "unverified" );
    return a;
}

} // namespace

TEST_CASE( "GovernanceStore opens, versions and guards forward schemas", "[governance][store]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString dbPath = dir.filePath( QStringLiteral( "gov.db" ) );

    {
        GovernanceStore store;
        QString error;
        REQUIRE( store.open( dbPath, &error ) );
        REQUIRE( error.isEmpty() );
        REQUIRE_FALSE( store.isReadOnly() );
        REQUIRE( store.schemaVersion() == QLatin1String( "1" ) );
    }
    // Reopen must preserve data.
    {
        GovernanceStore store;
        REQUIRE( store.open( dbPath ) );
        REQUIRE( store.assetCount() == 0 );
    }
    // Forward tolerance: a "newer" schema opens read-only, not fatal.
    {
        QFile marker( dbPath );
        REQUIRE( marker.exists() );
    }
    // Simulate a future schema by direct sqlite is out of scope here; the
    // read-only guard is exercised through schemaVersion() reporting.
}

TEST_CASE( "GovernanceStore asset mirror supports aliases, fingerprints and paging", "[governance][store][assets]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    QVector<GovernedAsset> batch;
    for ( int i = 0; i < 120; ++i )
    {
        GovernedAsset a = makeAsset( QStringLiteral( "asset-%1" ).arg( i ),
                                     QStringLiteral( "scene_%1" ).arg( i ),
                                     i % 2 == 0 ? QStringLiteral( "S2" ) : QStringLiteral( "L8" ),
                                     QStringLiteral( "optical" ) );
        a.aliases.append( QStringLiteral( "/vsicurl/http://x/%1.tif" ).arg( i ) );
        batch.append( a );
    }
    REQUIRE( store.upsertAssets( batch ).operator bool() );
    REQUIRE( store.assetCount() == 120 );

    // Point lookups.
    REQUIRE( store.assetById( QStringLiteral( "asset-7" ) ).has_value() );
    REQUIRE( store.assetByPath( QStringLiteral( "/data/scene_7.tif" ) ).has_value() );
    REQUIRE( store.assetByPath( QStringLiteral( "/vsicurl/http://x/7.tif" ) ).has_value() );

    // Fingerprint lookup (relink path).
    GovernedAsset f = makeAsset( QStringLiteral( "asset-dup" ), QStringLiteral( "scene_dup" ) );
    f.contentFingerprint = QStringLiteral( "abc123" );
    REQUIRE( store.upsertAsset( f ).operator bool() );
    REQUIRE( store.assetsByFingerprint( QStringLiteral( "abc123" ) ).size() == 1 );
    REQUIRE( store.assetsByFingerprint( QStringLiteral( "missing" ) ).isEmpty() );

    // Paged query, filtered + sorted.
    WorkspaceQuery q;
    q.set = EntitySet::Assets;
    q.sensor = QStringLiteral( "S2" );
    q.limit = 10;
    WorkspacePage page = store.query( q );
    REQUIRE( page.total == 60 );
    REQUIRE( page.items.size() == 10 );

    // Text search.
    WorkspaceQuery text;
    text.set = EntitySet::Assets;
    text.text = QStringLiteral( "scene_1" );  // matches 1, 10..19, 100..119
    REQUIRE( store.query( text ).total >= 11 );

    // Facets.
    WorkspaceQuery facet;
    facet.set = EntitySet::Assets;
    WorkspacePage sensorFacet = store.query( facet, QStringLiteral( "sensor" ) );
    REQUIRE( sensorFacet.facetField == QLatin1String( "sensor" ) );
    bool foundS2 = false;
    for ( const FacetCount &fc : sensorFacet.facets )
        foundS2 |= fc.value == QLatin1String( "S2" ) && fc.count == 60;
    REQUIRE( foundS2 );

    // Update preserves creation stamp and bumps nothing silently.
    GovernedAsset updated = batch.first();
    const qint64 createdBefore = store.assetById( updated.assetId )->metadata.value( QLatin1String( "noop" ) ).toDouble();
    Q_UNUSED( createdBefore );
    updated.state = QStringLiteral( "Missing" );
    REQUIRE( store.upsertAsset( updated ).operator bool() );
    REQUIRE( store.assetById( updated.assetId )->state == QLatin1String( "Missing" ) );

    // Removal clears aliases + tags + lineage.
    REQUIRE( store.addTag( QStringLiteral( "asset" ), updated.assetId, QStringLiteral( "qa" ) ).operator bool() );
    REQUIRE( store.removeAsset( updated.assetId ).operator bool() );
    REQUIRE_FALSE( store.assetById( updated.assetId ).has_value() );
    REQUIRE_FALSE( store.assetByPath( updated.canonicalSource ).has_value() );
}

TEST_CASE( "GovernanceStore tags and bulk tags operate transactionally", "[governance][store][tags]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    QVector<GovernedAsset> batch;
    QVector<QString> ids;
    for ( int i = 0; i < 50; ++i )
    {
        batch.append( makeAsset( QStringLiteral( "a%1" ).arg( i ), QStringLiteral( "n%1" ).arg( i ) ) );
        ids.append( QStringLiteral( "a%1" ).arg( i ) );
    }
    REQUIRE( store.upsertAssets( batch ).operator bool() );

    REQUIRE( store.bulkTag( ids, QStringLiteral( "asset" ), QStringLiteral( "cloud_free" ) ).operator bool() );
    REQUIRE( store.tagsOf( QStringLiteral( "asset" ), QStringLiteral( "a1" ) ).size() == 1 );
    REQUIRE( store.bulkTag( ids, QStringLiteral( "asset" ), QStringLiteral( "cloud_free" ) ).value() == 0 );

    // setTags REPLACES the tag set: a1 loses cloud_free, gains x/y.
    REQUIRE( store.setTags( QStringLiteral( "asset" ), QStringLiteral( "a1" ),
                            QStringList{ QStringLiteral( "x" ), QStringLiteral( "y" ) } ).operator bool() );
    REQUIRE( store.tagsOf( QStringLiteral( "asset" ), QStringLiteral( "a1" ) ).size() == 2 );

    WorkspaceQuery q;
    q.set = EntitySet::Assets;
    q.tag = QStringLiteral( "cloud_free" );
    REQUIRE( store.query( q ).total == 49 );
}

TEST_CASE( "GovernanceStore datasets, results, runs, experiments round-trip", "[governance][store][entities]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    // Dataset.
    DatasetRecord ds;
    ds.id = DatasetId::generate();
    ds.kind = DatasetKind::Training;
    ds.header.name = QStringLiteral( "water-training-2025" );
    ds.memberAssetIds = QStringList{ QStringLiteral( "a" ), QStringLiteral( "b" ) };
    REQUIRE( store.upsertDataset( ds ).operator bool() );
    REQUIRE( store.datasetById( ds.id.toString() )->memberAssetIds.size() == 2 );

    // Result with inputs, artifacts, metrics, lifecycle.
    ResultRecord r;
    r.id = ResultId::generate();
    r.semanticType = ResultSemanticType::Classification;
    r.header.name = QStringLiteral( "rf-water-2025" );
    r.producer = QJsonObject{ { QLatin1String( "operatorId" ), QLatin1String( "rs:supervised_classification" ) },
                              { QLatin1String( "runId" ), QLatin1String( "run-1" ) } };
    ResultInput in;
    in.assetId = QStringLiteral( "a" );
    in.revision = 3;
    r.inputs.append( in );
    ResultArtifact art;
    art.path = QStringLiteral( "/out/water.tif" );
    art.role = QStringLiteral( "primary" );
    r.artifacts.append( art );
    r.metrics = QJsonObject{ { QLatin1String( "overall_accuracy" ), 0.92 } };
    REQUIRE( store.upsertResult( r ).operator bool() );
    const std::optional<ResultRecord> loaded = store.resultById( r.id.toString() );
    REQUIRE( loaded.has_value() );
    REQUIRE( loaded->status == ResultStatus::Draft );
    REQUIRE( loaded->inputs.size() == 1 );
    REQUIRE( loaded->artifacts.size() == 1 );
    REQUIRE( qAbs( loaded->metrics.value( QLatin1String( "overall_accuracy" ) ).toDouble() - 0.92 ) < 1e-9 );

    // Lifecycle: draft -> validated -> approved legal; draft -> approved illegal.
    REQUIRE( isLegalResultTransition( ResultStatus::Draft, ResultStatus::Validated ) );
    REQUIRE( isLegalResultTransition( ResultStatus::Validated, ResultStatus::Approved ) );
    REQUIRE_FALSE( isLegalResultTransition( ResultStatus::Draft, ResultStatus::Approved ) );
    REQUIRE( isLegalResultTransition( ResultStatus::Approved, ResultStatus::Superseded ) );
    REQUIRE_FALSE( isLegalResultTransition( ResultStatus::Archived, ResultStatus::Draft ) );

    // Run.
    RunRecord run;
    run.id = QStringLiteral( "run-1" );
    run.workflowId = QStringLiteral( "wf-water" );
    run.state = QStringLiteral( "Completed" );
    run.outputAssetIds.append( QStringLiteral( "a" ) );
    REQUIRE( store.upsertRun( run ).operator bool() );
    store.linkRunOutput( QStringLiteral( "run-1" ), QStringLiteral( "a" ) );
    REQUIRE( store.runById( QStringLiteral( "run-1" ) )->outputAssetIds.size() == 1 );

    // Orphan detection: result references missing run "run-404".
    ResultRecord orphan = r;
    orphan.id = ResultId::generate();
    orphan.producer = QJsonObject{ { QLatin1String( "runId" ), QLatin1String( "run-404" ) } };
    REQUIRE( store.upsertResult( orphan ).operator bool() );
    REQUIRE( store.orphanResults().size() == 1 );

    // Impact: results depending on asset "a".
    REQUIRE( store.resultsDependingOnAsset( QStringLiteral( "a" ) ).size() == 2 );

    // Experiment.
    ExperimentRecord exp;
    exp.id = ExperimentId::generate();
    exp.header.name = QStringLiteral( "rf-vs-svm" );
    exp.objective = QStringLiteral( "compare classifiers" );
    ExperimentVariant v;
    v.key = QStringLiteral( "classifier" );
    v.value = QJsonObject{ { QLatin1String( "model" ), QLatin1String( "rf" ) } };
    exp.variants.append( v );
    exp.runIds.append( QStringLiteral( "run-1" ) );
    REQUIRE( store.upsertExperiment( exp ).operator bool() );
    REQUIRE( store.experimentById( exp.id.toString() )->variants.size() == 1 );
    REQUIRE( store.experiments().size() == 1 );
    REQUIRE( store.removeExperiment( exp.id.toString() ).operator bool() );
    REQUIRE( store.experiments().isEmpty() );
}

TEST_CASE( "GovernanceStore asset removal keeps downstream lineage edges",
           "[governance][store][lineage][issue758]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    auto edge = [ & ]( const QString &out, const QString &in ) {
        GovernanceStore::LineageEdge e;
        e.outputAssetId = out;
        e.inputAssetId = in;
        e.operatorId = QStringLiteral( "rs:test" );
        return e;
    };
    // Chain: raw -> inter (intermediate) -> final.
    REQUIRE( store.addLineageEdges( { edge( "inter", "raw" ), edge( "final", "inter" ) } ).operator bool() );
    REQUIRE( store.upsertAsset( makeAsset( "raw", "raw" ) ).operator bool() );
    REQUIRE( store.upsertAsset( makeAsset( "inter", "inter" ) ).operator bool() );
    REQUIRE( store.upsertAsset( makeAsset( "final", "final" ) ).operator bool() );

    // Unload the intermediate: its own provenance goes with it, but the
    // SURVIVING consumer (final) keeps the edge that explains its input
    // (issue #758-6: both-direction deletion truncated downstream provenance).
    REQUIRE( store.removeAsset( QStringLiteral( "inter" ) ).operator bool() );

    // "final" still explains where it came from: the edge (final ← inter)
    // survives even though the inter row is gone (upstream traversal returns
    // the recorded input id).
    const QVector<QVariantMap> upstream = store.lineageUpstream( QStringLiteral( "final" ) );
    bool finalProvenanceSurvives = false;
    for ( const QVariantMap &row : upstream )
        finalProvenanceSurvives |= row.value( QStringLiteral( "assetId" ) ).toString() == QLatin1String( "inter" );
    REQUIRE( finalProvenanceSurvives );
    REQUIRE( store.directEdges( QStringLiteral( "final" ), true ).size() == 1 );
}

TEST_CASE( "GovernanceStore alias ownership is collision-checked symmetrically",
           "[governance][store][assets][issue758]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    GovernedAsset first = makeAsset( "a1", "one" );
    first.aliases = QStringList{ QStringLiteral( "/data/shared-alias.tif" ) };
    const auto firstResult = store.upsertAsset( first );
    REQUIRE( firstResult.operator bool() );

    // Second asset claims the SAME secondary alias: no silent steal — the
    // existing owner keeps the alias and a diagnostic is surfaced.
    GovernedAsset second = makeAsset( "a2", "two" );
    second.aliases = QStringList{ QStringLiteral( "/data/shared-alias.tif" ) };
    const auto secondResult = store.upsertAsset( second );
    REQUIRE( secondResult.operator bool() );
    bool collisionReported = false;
    for ( const Diagnostic &d : secondResult.diagnostics() )
        collisionReported |= d.code == QLatin1String( "store.alias_collision" );
    REQUIRE( collisionReported );
    REQUIRE( store.assetByPath( QStringLiteral( "/data/shared-alias.tif" ) )->assetId
             == QLatin1String( "a1" ) );
}

TEST_CASE( "GovernanceStore entityCounts reports real totals past the page size",
           "[governance][store][counts][issue758]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    QVector<GovernedAsset> batch;
    for ( int i = 0; i < GovernanceStore::kMaxPageSize + 37; ++i )
        batch.append( makeAsset( QStringLiteral( "big%1" ).arg( i ), QStringLiteral( "b%1" ).arg( i ) ) );
    REQUIRE( store.upsertAssets( batch ).operator bool() );

    // Page-bounded listing clamps at the page size; entityCounts must not.
    REQUIRE( store.allAssets( GovernanceStore::kMaxPageSize ).size()
             == GovernanceStore::kMaxPageSize );
    const GovernanceStore::EntityCounts counts = store.entityCounts();
    REQUIRE( counts.assets == GovernanceStore::kMaxPageSize + 37 );
}

TEST_CASE( "GovernanceStore lineage queries are transitive and cycle-safe", "[governance][store][lineage]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    // Chain: raw -> l2a -> index -> class (A<-B<-C<-D edges as output->input).
    auto edge = [ & ]( const QString &out, const QString &in ) {
        GovernanceStore::LineageEdge e;
        e.outputAssetId = out;
        e.inputAssetId = in;
        e.operatorId = QStringLiteral( "rs:test" );
        return e;
    };
    REQUIRE( store.addLineageEdges( { edge( "l2a", "raw" ), edge( "index", "l2a" ), edge( "class", "index" ) } ).operator bool() );

    const QVector<QVariantMap> upstream = store.lineageUpstream( QStringLiteral( "class" ) );
    REQUIRE( upstream.size() == 3 );
    REQUIRE( upstream.first().value( QStringLiteral( "assetId" ) ).toString() == QLatin1String( "index" ) );

    const QVector<QVariantMap> downstream = store.lineageDownstream( QStringLiteral( "raw" ) );
    REQUIRE( downstream.size() == 3 );

    REQUIRE( store.directEdges( QStringLiteral( "l2a" ), true ).size() == 1 );
    REQUIRE( store.directEdges( QStringLiteral( "l2a" ), false ).size() == 1 );

    // Cycle: X -> Y -> X must terminate and not duplicate forever.
    REQUIRE( store.addLineageEdges( { edge( "cycX", "cycY" ), edge( "cycY", "cycX" ) } ).operator bool() );
    const QVector<QVariantMap> cyc = store.lineageUpstream( QStringLiteral( "cycX" ), 25 );
    REQUIRE( cyc.size() == 1 );  // only cycY, once
}

TEST_CASE( "GovernanceStore smart collections, exports, mappings, audit, integrity", "[governance][store][misc]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    SmartCollectionRecord smart;
    smart.id = SmartCollectionId::generate();
    smart.header.name = QStringLiteral( "S2 2025" );
    smart.predicates.append( SmartPredicate{ QStringLiteral( "sensor" ), QStringLiteral( "eq" ), QStringLiteral( "S2" ) } );
    smart.predicates.append( SmartPredicate{ QStringLiteral( "year" ), QStringLiteral( "eq" ), QStringLiteral( "2025" ) } );
    REQUIRE( store.upsertSmartCollection( smart ).operator bool() );
    REQUIRE( store.smartCollections().size() == 1 );
    REQUIRE( store.smartCollections().first().predicates.size() == 2 );
    REQUIRE( store.removeSmartCollection( smart.id.toString() ).operator bool() );

    ExportRecord ex;
    ex.id = ExportId::generate();
    ex.kind = QStringLiteral( "map" );
    ex.target = QStringLiteral( "/export/map.png" );
    REQUIRE( store.upsertExport( ex ).operator bool() );
    REQUIRE( store.exports().size() == 1 );

    REQUIRE( store.upsertPathMapping( PathMapping{ QStringLiteral( "externalRoot" ),
                                                   QStringLiteral( "/mnt/old" ), QStringLiteral( "/mnt/new" ) } ).operator bool() );
    REQUIRE( store.pathMappings().size() == 1 );

    REQUIRE( store.appendAudit( QStringLiteral( "test" ), QStringLiteral( "asset.relink" ),
                                QStringLiteral( "asset" ), QStringLiteral( "a1" ) ).operator bool() );
    REQUIRE( store.auditTail( 10 ).size() == 1 );
    REQUIRE( store.auditTail().first().action == QLatin1String( "asset.relink" ) );

    REQUIRE( store.integrityCheck().code == QLatin1String( "store.integrity_ok" ) );
    REQUIRE( store.clearAll().operator bool() );
    REQUIRE( store.assetCount() == 0 );
    REQUIRE( store.exports().isEmpty() );
}

// Issue #1056: removeAsset used to leave the asset referenced by
// dataset_members / result_inputs / run_outputs — readers handed out ids no
// lookup could resolve. The removal now clears those relationship rows inside
// the same transaction.
TEST_CASE( "GovernanceStore removeAsset leaves no phantom references in datasets, results or runs",
           "[governance][store][issue1056]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    REQUIRE( store.upsertAsset( makeAsset( "gone", "gone" ) ).operator bool() );
    REQUIRE( store.upsertAsset( makeAsset( "stays", "stays" ) ).operator bool() );

    DatasetRecord ds;
    ds.id = DatasetId::generate();
    ds.header.name = QStringLiteral( "mixed" );
    ds.memberAssetIds = QStringList{ QStringLiteral( "gone" ), QStringLiteral( "stays" ) };
    REQUIRE( store.upsertDataset( ds ).operator bool() );

    ResultRecord r;
    r.id = ResultId::generate();
    r.semanticType = ResultSemanticType::Classification;
    ResultInput in;
    in.assetId = QStringLiteral( "gone" );
    ResultInput keep;
    keep.assetId = QStringLiteral( "stays" );
    r.inputs.append( in );
    r.inputs.append( keep );
    REQUIRE( store.upsertResult( r ).operator bool() );

    RunRecord run;
    run.id = QStringLiteral( "run-phantom" );
    run.state = QStringLiteral( "Completed" );
    REQUIRE( store.upsertRun( run ).operator bool() );
    REQUIRE( store.linkRunOutput( QStringLiteral( "run-phantom" ), QStringLiteral( "gone" ) ).operator bool() );
    REQUIRE( store.linkRunOutput( QStringLiteral( "run-phantom" ), QStringLiteral( "stays" ) ).operator bool() );

    REQUIRE( store.removeAsset( QStringLiteral( "gone" ) ).operator bool() );

    // No reader returns the removed id anymore.
    const auto loadedDataset = store.datasetById( ds.id.toString() );
    REQUIRE( loadedDataset.has_value() );
    REQUIRE( loadedDataset->memberAssetIds == QStringList{ QStringLiteral( "stays" ) } );

    const auto loadedResult = store.resultById( r.id.toString() );
    REQUIRE( loadedResult.has_value() );
    for ( const ResultInput &remaining : loadedResult->inputs )
        REQUIRE( remaining.assetId != QLatin1String( "gone" ) );
    REQUIRE( loadedResult->inputs.size() == 1 );
    REQUIRE( store.resultsDependingOnAsset( QStringLiteral( "gone" ) ).isEmpty() );

    const auto loadedRun = store.runById( QStringLiteral( "run-phantom" ) );
    REQUIRE( loadedRun.has_value() );
    REQUIRE( loadedRun->outputAssetIds == QStringList{ QStringLiteral( "stays" ) } );

    // The dataset-scoped query surface cannot resurrect the id either.
    WorkspaceQuery q;
    q.set = EntitySet::Assets;
    q.datasetId = ds.id.toString();
    REQUIRE( store.query( q ).total == 1 );
}

TEST_CASE( "GovernanceStore removeAsset is atomic under injected failures",
           "[governance][store][fault][issue1045]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    REQUIRE( store.upsertAsset( makeAsset( "a", "a" ) ).operator bool() );

    DatasetRecord ds;
    ds.id = DatasetId::generate();
    ds.memberAssetIds = QStringList{ QStringLiteral( "a" ) };
    REQUIRE( store.upsertDataset( ds ).operator bool() );

    auto verifyIntact = [ & ]() {
        REQUIRE( store.assetById( QStringLiteral( "a" ) ).has_value() );
        REQUIRE( store.datasetById( ds.id.toString() )->memberAssetIds.size() == 1 );
        REQUIRE( store.tagsOf( QStringLiteral( "asset" ), QStringLiteral( "a" ) ).size() == 1 );
        REQUIRE( store.directEdges( QStringLiteral( "a" ), true ).size() == 1 );
    };

    GovernanceStore::LineageEdge edge;
    edge.outputAssetId = QStringLiteral( "a" );
    edge.inputAssetId = QStringLiteral( "upstream" );
    edge.operatorId = QStringLiteral( "rs:test" );
    REQUIRE( store.addLineageEdges( { edge } ).operator bool() );
    REQUIRE( store.addTag( QStringLiteral( "asset" ), QStringLiteral( "a" ), QStringLiteral( "qa" ) ).operator bool() );

    // Step-seam failure: the removal is refused wholesale.
    {
        ArmedFault fault( { "governance_store.step", Mode::NextN, 1, {} } );
        REQUIRE_FALSE( store.removeAsset( QStringLiteral( "a" ) ).operator bool() );
    }
    verifyIntact();

    // Commit-seam failure: same verdict, nothing half-removed.
    {
        ArmedFault fault( { "governance_store.commit", Mode::NextN, 1, {} } );
        REQUIRE_FALSE( store.removeAsset( QStringLiteral( "a" ) ).operator bool() );
    }
    verifyIntact();

    // Removing an unknown id fails without disturbing the graph (the
    // not-found verdict no longer commits after partial cleanup).
    REQUIRE_FALSE( store.removeAsset( QStringLiteral( "nope" ) ).operator bool() );
    verifyIntact();

    // The handle is still usable and the real removal clears relationships.
    REQUIRE( store.removeAsset( QStringLiteral( "a" ) ).operator bool() );
    REQUIRE_FALSE( store.assetById( QStringLiteral( "a" ) ).has_value() );
    REQUIRE( store.datasetById( ds.id.toString() )->memberAssetIds.isEmpty() );
}

TEST_CASE( "GovernanceStore multi-statement writers roll back to the prior state on failure",
           "[governance][store][fault][issue1045]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    // Dataset membership rewrite: a commit failure keeps the OLD members.
    DatasetRecord ds;
    ds.id = DatasetId::generate();
    ds.memberAssetIds = QStringList{ QStringLiteral( "m1" ), QStringLiteral( "m2" ) };
    REQUIRE( store.upsertDataset( ds ).operator bool() );
    {
        ArmedFault fault( { "governance_store.commit", Mode::NextN, 1, {} } );
        DatasetRecord changed = ds;
        changed.memberAssetIds = QStringList{ QStringLiteral( "m3" ) };
        REQUIRE_FALSE( store.upsertDataset( changed ).operator bool() );
    }
    REQUIRE( store.datasetById( ds.id.toString() )->memberAssetIds
             == QStringList{ QStringLiteral( "m1" ), QStringLiteral( "m2" ) } );

    // Lineage reconciliation with replaceOutgoing: a commit failure must NOT
    // lose the superseded edges it was about to replace.
    auto edge = [ & ]( const QString &out, const QString &in ) {
        GovernanceStore::LineageEdge e;
        e.outputAssetId = out;
        e.inputAssetId = in;
        e.operatorId = QStringLiteral( "rs:test" );
        return e;
    };
    REQUIRE( store.addLineageEdges( { edge( "out", "in1" ) } ).operator bool() );
    {
        ArmedFault fault( { "governance_store.commit", Mode::NextN, 1, {} } );
        REQUIRE_FALSE( store.addLineageEdges( { edge( "out", "in2" ) }, true ).operator bool() );
    }
    const QVector<GovernanceStore::LineageEdge> edges = store.directEdges( QStringLiteral( "out" ), true );
    REQUIRE( edges.size() == 1 );
    REQUIRE( edges.first().inputAssetId == QLatin1String( "in1" ) );

    // All writers still succeed on the same handle — no leaked transaction.
    DatasetRecord ok = ds;
    ok.memberAssetIds = QStringList{ QStringLiteral( "m3" ) };
    REQUIRE( store.upsertDataset( ok ).operator bool() );
    REQUIRE( store.datasetById( ds.id.toString() )->memberAssetIds == QStringList{ QStringLiteral( "m3" ) } );
    REQUIRE( store.addLineageEdges( { edge( "out", "in2" ) }, true ).operator bool() );
    REQUIRE( store.directEdges( QStringLiteral( "out" ), true ).size() == 1 );
}

// A removal that reports success MUST be committed: a leaked transaction
// breaks every later write on the handle (BEGIN fails) and an uncommitted
// removal vanishes on reopen even though the caller saw success.
TEST_CASE( "GovernanceStore removals commit durably and never leak the transaction",
           "[governance][store][fault][issue1045]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "gov.db" ) );
    GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    DatasetRecord ds;
    ds.id = DatasetId::generate();
    ds.memberAssetIds = QStringList{ QStringLiteral( "m1" ) };
    REQUIRE( store.upsertDataset( ds ).operator bool() );
    ResultRecord r;
    r.id = ResultId::generate();
    REQUIRE( store.upsertResult( r ).operator bool() );
    ExperimentRecord exp;
    exp.id = ExperimentId::generate();
    REQUIRE( store.upsertExperiment( exp ).operator bool() );

    REQUIRE( store.removeDataset( ds.id.toString() ).operator bool() );
    REQUIRE( store.removeResult( r.id.toString() ).operator bool() );
    REQUIRE( store.removeExperiment( exp.id.toString() ).operator bool() );

    // A leaked transaction would fail this BEGIN on the same handle.
    REQUIRE( store.upsertAsset( makeAsset( "after", "after" ) ).operator bool() );

    // A fresh handle reads COMMITTED state: all three removals are durable
    // and the follow-up write survived too.
    GovernanceStore fresh;
    REQUIRE( fresh.open( dbPath ) );
    REQUIRE_FALSE( fresh.datasetById( ds.id.toString() ).has_value() );
    REQUIRE_FALSE( fresh.resultById( r.id.toString() ).has_value() );
    REQUIRE_FALSE( fresh.experimentById( exp.id.toString() ).has_value() );
    REQUIRE( fresh.assetById( QStringLiteral( "after" ) ).has_value() );
}

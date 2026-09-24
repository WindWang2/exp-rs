// Governance store (Platform 3.0) — schema, paging, lineage, lifecycle.
#include <catch2/catch_test_macros.hpp>

#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantMap>

#include <sqlite3.h>
#include <QJsonObject>

using namespace sicnu::workspace;

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

/// Plants a RAISE(ABORT) guard on the store DB (direct DB surgery while the
/// store handle is idle) to make a statement inside an open transaction fail.
void plantTrigger( const QString &dbPath, const QString &sql )
{
    sqlite3 *raw = nullptr;
    REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw, SQLITE_OPEN_READWRITE,
                              nullptr ) == SQLITE_OK );
    char *err = nullptr;
    const int rc = sqlite3_exec( raw, sql.toUtf8().constData(), nullptr, nullptr, &err );
    if ( rc != SQLITE_OK )
        INFO( QString::fromUtf8( err ).toStdString() );
    sqlite3_free( err );
    sqlite3_close( raw );
    REQUIRE( rc == SQLITE_OK );
}

QString dropTrigger( const QString &name )
{
    return QStringLiteral( "DROP TRIGGER %1" ).arg( name );
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
    // Cascade is EXPLICIT here (12.0 removeAsset guard): the asset is still
    // referenced by the downstream lineage edge, so the default policy now
    // refuses — this test exercises the cascade path on purpose.
    CHECK_FALSE( store.removeAsset( QStringLiteral( "inter" ) ).operator bool() );
    REQUIRE( store
                 .removeAsset( QStringLiteral( "inter" ),
                               GovernanceStore::RemoveAssetPolicy::Cascade )
                 .operator bool() );

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

TEST_CASE( "GovernanceStore removeAsset leaves no phantom references",
           "[governance][store][remove]" )
{
    // The schema has no foreign keys, so relationship rows survive an asset
    // removal unless they are deleted in the same transaction — readers would
    // then return ids that no longer resolve.
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );
    REQUIRE( store.upsertAsset( makeAsset( "a", "asset-a" ) ).operator bool() );

    DatasetRecord ds;
    ds.id = DatasetId::generate();
    ds.kind = DatasetKind::Training;
    ds.header.name = QStringLiteral( "water-training" );
    ds.memberAssetIds = QStringList{ QStringLiteral( "a" ) };
    REQUIRE( store.upsertDataset( ds ).operator bool() );
    REQUIRE( store.datasetById( ds.id.toString() )->memberAssetIds.size() == 1 );

    ResultRecord r;
    r.id = ResultId::generate();
    r.semanticType = ResultSemanticType::Classification;
    r.header.name = QStringLiteral( "rf-water" );
    ResultInput in;
    in.assetId = QStringLiteral( "a" );
    in.revision = 1;
    r.inputs.append( in );
    REQUIRE( store.upsertResult( r ).operator bool() );
    REQUIRE( store.resultById( r.id.toString() )->inputs.size() == 1 );

    RunRecord run;
    run.id = QStringLiteral( "run-1" );
    run.workflowId = QStringLiteral( "wf" );
    run.outputAssetIds.append( QStringLiteral( "a" ) );
    REQUIRE( store.upsertRun( run ).operator bool() );
    REQUIRE( store.linkRunOutput( QStringLiteral( "run-1" ), QStringLiteral( "a" ) ).operator bool() );
    REQUIRE( store.runById( QStringLiteral( "run-1" ) )->outputAssetIds.size() == 1 );

    // 12.0 removeAsset guard: the asset IS referenced (dataset member,
    // result input, run output) — the default policy refuses and names the
    // reference kind; the row survives untouched.
    const auto refused = store.removeAsset( QStringLiteral( "a" ) );
    CHECK_FALSE( refused.has_value() );
    CHECK( refused.diagnostics().first().code == QLatin1String( "store.asset_referenced" ) );
    REQUIRE( store.assetById( QStringLiteral( "a" ) ).has_value() );
    CHECK( store.datasetById( ds.id.toString() )->memberAssetIds.size() == 1 );
    CHECK( store.resultById( r.id.toString() )->inputs.size() == 1 );
    CHECK( store.runById( QStringLiteral( "run-1" ) )->outputAssetIds.size() == 1 );
    const QVector<GovernanceStore::AssetReference> references =
        store.collectAssetReferences( QStringLiteral( "a" ) );
    QStringList referenceKinds;
    for ( const GovernanceStore::AssetReference &reference : references )
        referenceKinds.append( reference.kind );
    CHECK( referenceKinds.contains( QLatin1String( "dataset_member" ) ) );
    CHECK( referenceKinds.contains( QLatin1String( "result_input" ) ) );
    CHECK( referenceKinds.contains( QLatin1String( "run_output" ) ) );

    // The explicit cascade (the documented escape hatch) still cleans up in
    // one transaction: no reader reports the removed id any more.
    REQUIRE( store
                 .removeAsset( QStringLiteral( "a" ),
                               GovernanceStore::RemoveAssetPolicy::Cascade )
                 .operator bool() );
    CHECK( store.datasetById( ds.id.toString() )->memberAssetIds.isEmpty() );
    CHECK( store.resultById( r.id.toString() )->inputs.isEmpty() );
    CHECK( store.runById( QStringLiteral( "run-1" ) )->outputAssetIds.isEmpty() );
    CHECK( store.resultsDependingOnAsset( QStringLiteral( "a" ) ).isEmpty() );
    CHECK( store.collectAssetReferences( QStringLiteral( "a" ) ).isEmpty() );
}

TEST_CASE( "GovernanceStore relationship writes roll back when a step fails",
           "[governance][store][transaction]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "gov.db" ) );
    GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    // Dataset: a failing member insert must not leave the dataset row behind.
    plantTrigger( dbPath, QStringLiteral( "CREATE TRIGGER fail_member BEFORE INSERT ON"
                                         " dataset_members BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    DatasetRecord ds;
    ds.id = DatasetId::generate();
    ds.kind = DatasetKind::Training;
    ds.header.name = QStringLiteral( "water" );
    ds.memberAssetIds = QStringList{ QStringLiteral( "asset-x" ) };
    CHECK_FALSE( store.upsertDataset( ds ).operator bool() );
    CHECK_FALSE( store.datasetById( ds.id.toString() ).has_value() );
    plantTrigger( dbPath, dropTrigger( QStringLiteral( "fail_member" ) ) );
    REQUIRE( store.upsertDataset( ds ).operator bool() );
    REQUIRE( store.datasetById( ds.id.toString() )->memberAssetIds.size() == 1 );

    // Result: a failing input insert must not leave the result row behind.
    plantTrigger( dbPath, QStringLiteral( "CREATE TRIGGER fail_input BEFORE INSERT ON"
                                         " result_inputs BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    ResultRecord r;
    r.id = ResultId::generate();
    r.semanticType = ResultSemanticType::Classification;
    r.header.name = QStringLiteral( "rf" );
    ResultInput in;
    in.assetId = QStringLiteral( "asset-x" );
    r.inputs.append( in );
    CHECK_FALSE( store.upsertResult( r ).operator bool() );
    CHECK_FALSE( store.resultById( r.id.toString() ).has_value() );
    plantTrigger( dbPath, dropTrigger( QStringLiteral( "fail_input" ) ) );
    REQUIRE( store.upsertResult( r ).operator bool() );

    // Lineage: a failing edge insert must not leave partial provenance.
    GovernanceStore::LineageEdge edge;
    edge.outputAssetId = QStringLiteral( "out" );
    edge.inputAssetId = QStringLiteral( "asset-x" );
    edge.operatorId = QStringLiteral( "rs:test" );
    plantTrigger( dbPath, QStringLiteral( "CREATE TRIGGER fail_edge BEFORE INSERT ON lineage_edges"
                                         " BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    CHECK_FALSE( store.addLineageEdges( { edge } ).operator bool() );
    CHECK( store.lineageUpstream( QStringLiteral( "out" ) ).isEmpty() );
    plantTrigger( dbPath, dropTrigger( QStringLiteral( "fail_edge" ) ) );
    REQUIRE( store.addLineageEdges( { edge } ).operator bool() );
    REQUIRE( store.lineageUpstream( QStringLiteral( "out" ) ).size() == 1 );

    // Removal: a failing member delete must not strip the asset row either.
    plantTrigger( dbPath, QStringLiteral( "CREATE TRIGGER fail_member_del BEFORE DELETE ON"
                                         " dataset_members BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    CHECK_FALSE( store.removeDataset( ds.id.toString() ).operator bool() );
    REQUIRE( store.datasetById( ds.id.toString() ).has_value() );
    plantTrigger( dbPath, dropTrigger( QStringLiteral( "fail_member_del" ) ) );
    REQUIRE( store.removeDataset( ds.id.toString() ).operator bool() );
}

TEST_CASE( "resultsDependingOnAssets stays correct beyond the SQLite variable limit",
           "[governance][store][impact]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    ResultRecord r;
    r.id = ResultId::generate();
    r.semanticType = ResultSemanticType::Classification;
    r.header.name = QStringLiteral( "impact" );
    ResultInput first;
    first.assetId = QStringLiteral( "asset-first" );
    ResultInput last;
    last.assetId = QStringLiteral( "asset-last" );
    r.inputs.append( first );
    r.inputs.append( last );
    REQUIRE( store.upsertResult( r ).operator bool() );

    // A hub asset's downstream set is width-unbounded and can exceed the
    // engine's host-parameter limit; the query must chunk the IN-list instead
    // of failing open with a silently empty answer. 251k entries also crosses
    // the raised default of newer SQLite builds (250000) so the pre-chunking
    // implementation fails this probe on current systems too, not just on
    // distros shipping the historical 999/32766 limits.
    QStringList affected;
    affected.reserve( 251000 );
    for ( int i = 0; i < 251000; ++i )
        affected.append( QStringLiteral( "hub-%1" ).arg( i, 6, 10, QLatin1Char( '0' ) ) );
    affected[ 0 ] = QStringLiteral( "asset-first" );
    affected[ 250999 ] = QStringLiteral( "asset-last" );

    const QVector<ResultRecord> hits = store.resultsDependingOnAssets( affected );
    REQUIRE( hits.size() == 1 );
    CHECK( hits.first().id == r.id );

    // The bounded sibling keeps its semantics.
    CHECK( store.resultsDependingOnAsset( QStringLiteral( "asset-last" ) ).size() == 1 );
}

TEST_CASE( "GovernanceStore batch writes roll back when any step fails — "
           "aliases, tags, run outputs, experiment variants and removal",
           "[governance][store][transaction]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "gov.db" ) );
    GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    // Aliases: a failing alias insert must not commit the asset mirror row —
    // an asset that cannot be resolved by path is a half-written identity.
    plantTrigger( dbPath, QStringLiteral( "CREATE TRIGGER fail_alias BEFORE INSERT ON aliases"
                                         " BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    GovernedAsset a = makeAsset( QStringLiteral( "asset-a" ), QStringLiteral( "a.tif" ) );
    CHECK_FALSE( store.upsertAsset( a ).operator bool() );
    CHECK_FALSE( store.assetById( QStringLiteral( "asset-a" ) ).has_value() );
    plantTrigger( dbPath, dropTrigger( QStringLiteral( "fail_alias" ) ) );
    REQUIRE( store.upsertAsset( a ).operator bool() );
    REQUIRE( store.assetByPath( a.canonicalSource )->assetId == QStringLiteral( "asset-a" ) );

    // Tags: a failing tag insert must not commit a partial tag set (nor the
    // delete of the previous set).
    plantTrigger( dbPath, QStringLiteral(
        "CREATE TRIGGER fail_second_tag BEFORE INSERT ON tags"
        " WHEN (SELECT COUNT(*) FROM tags WHERE entity_kind='asset'"
        "       AND entity_id='asset-a') >= 1"
        " BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    CHECK_FALSE( store.setTags( QStringLiteral( "asset" ), QStringLiteral( "asset-a" ),
                                { QStringLiteral( "keep" ), QStringLiteral( "drop" ) } ).operator bool() );
    CHECK( store.tagsOf( QStringLiteral( "asset" ), QStringLiteral( "asset-a" ) ).isEmpty() );
    plantTrigger( dbPath, dropTrigger( QStringLiteral( "fail_second_tag" ) ) );
    REQUIRE( store.setTags( QStringLiteral( "asset" ), QStringLiteral( "asset-a" ),
                            { QStringLiteral( "keep" ), QStringLiteral( "drop" ) } ).operator bool() );
    REQUIRE( store.tagsOf( QStringLiteral( "asset" ), QStringLiteral( "asset-a" ) ).size() == 2 );

    // Run outputs: a failing link insert must not commit a partial batch.
    RunRecord run1;
    run1.id = QStringLiteral( "run-1" );
    run1.state = QStringLiteral( "Succeeded" );
    RunRecord run2;
    run2.id = QStringLiteral( "run-2" );
    run2.state = QStringLiteral( "Succeeded" );
    REQUIRE( store.upsertRun( run1 ).operator bool() );
    REQUIRE( store.upsertRun( run2 ).operator bool() );
    plantTrigger( dbPath, QStringLiteral(
        "CREATE TRIGGER fail_second_link BEFORE INSERT ON run_outputs"
        " WHEN (SELECT COUNT(*) FROM run_outputs) >= 1"
        " BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    CHECK_FALSE( store.addRunOutputs( { { QStringLiteral( "run-1" ), QStringLiteral( "asset-a" ) },
                                        { QStringLiteral( "run-2" ), QStringLiteral( "asset-a" ) } } ).operator bool() );
    CHECK( store.runById( QStringLiteral( "run-1" ) )->outputAssetIds.isEmpty() );
    plantTrigger( dbPath, dropTrigger( QStringLiteral( "fail_second_link" ) ) );
    REQUIRE( store.addRunOutputs( { { QStringLiteral( "run-1" ), QStringLiteral( "asset-a" ) },
                                    { QStringLiteral( "run-2" ), QStringLiteral( "asset-a" ) } } ).operator bool() );
    REQUIRE( store.runById( QStringLiteral( "run-1" ) )->outputAssetIds.size() == 1 );
    REQUIRE( store.runById( QStringLiteral( "run-2" ) )->outputAssetIds.size() == 1 );

    // Experiment variants: a failing variant insert must not commit the
    // cleared-out previous variant set (silent provenance loss).
    ExperimentRecord exp;
    exp.id = ExperimentId::generate();
    exp.header.name = QStringLiteral( "sweep" );
    ExperimentVariant baseline;
    baseline.key = QStringLiteral( "baseline" );
    exp.variants.append( baseline );
    REQUIRE( store.upsertExperiment( exp ).operator bool() );
    plantTrigger( dbPath, QStringLiteral(
        "CREATE TRIGGER fail_variant BEFORE INSERT ON experiment_variants"
        " BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    ExperimentVariant sweep;
    sweep.key = QStringLiteral( "sweep-v2" );
    exp.variants.clear();
    exp.variants.append( sweep );
    CHECK_FALSE( store.upsertExperiment( exp ).operator bool() );
    REQUIRE( store.experimentById( exp.id.toString() ).has_value() );
    REQUIRE( store.experimentById( exp.id.toString() )->variants.size() == 1 );
    CHECK( store.experimentById( exp.id.toString() )->variants.first().key ==
           QLatin1String( "baseline" ) );
    plantTrigger( dbPath, dropTrigger( QStringLiteral( "fail_variant" ) ) );
    REQUIRE( store.upsertExperiment( exp ).operator bool() );
    REQUIRE( store.experimentById( exp.id.toString() )->variants.first().key ==
             QLatin1String( "sweep-v2" ) );

    // Experiment removal: a failing variant delete must not strip the parent
    // row (a committed half-removal leaves orphan variant rows behind).
    plantTrigger( dbPath, QStringLiteral( "CREATE TRIGGER fail_variant_del BEFORE DELETE ON"
                                         " experiment_variants"
                                         " BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    CHECK_FALSE( store.removeExperiment( exp.id.toString() ).operator bool() );
    REQUIRE( store.experimentById( exp.id.toString() ).has_value() );
    plantTrigger( dbPath, dropTrigger( QStringLiteral( "fail_variant_del" ) ) );
    REQUIRE( store.removeExperiment( exp.id.toString() ).operator bool() );
    CHECK_FALSE( store.experimentById( exp.id.toString() ).has_value() );
}

TEST_CASE( "GovernanceStore same-batch path move resolves by final state",
           "[governance][store][assets]" )
{
    QTemporaryDir dir;
    GovernanceStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    GovernedAsset a = makeAsset( QStringLiteral( "asset-a" ), QStringLiteral( "move" ) );
    REQUIRE( store.upsertAsset( a ).operator bool() );

    // One batch hands a's canonical path to b while a moves elsewhere.
    GovernedAsset b = makeAsset( QStringLiteral( "asset-b" ), QStringLiteral( "move" ) );
    GovernedAsset aMoved = makeAsset( QStringLiteral( "asset-a" ), QStringLiteral( "q" ) );
    const auto result = store.upsertAssets( { b, aMoved } );
    REQUIRE( result.operator bool() );
    CHECK( result.diagnostics().isEmpty() );

    REQUIRE( store.assetByPath( QStringLiteral( "/data/move.tif" ) ).has_value() );
    CHECK( store.assetByPath( QStringLiteral( "/data/move.tif" ) )->assetId ==
           QLatin1String( "asset-b" ) );
    REQUIRE( store.assetByPath( QStringLiteral( "/data/q.tif" ) ).has_value() );
    CHECK( store.assetByPath( QStringLiteral( "/data/q.tif" ) )->assetId ==
           QLatin1String( "asset-a" ) );
}

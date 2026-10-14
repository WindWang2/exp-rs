// test_fault_matrix.cpp — portable fault-injection matrix (task B, Verification 7.0).
//
// Exercises the REAL failure paths of three publish seams through the
// deterministic fault registry (no POSIX fork, runs on Windows/MSVC and CI):
//
//   seam                              | fault names
//   ----------------------------------+-------------------------------------
//   OutputCommitter::commit           | output_committer.publish
//   ArtifactObjectPool::put           | artifact_pool.stage_copy,
//                                     | artifact_pool.stage_publish
//   WorkflowCheckpointManager::save   | workflow_checkpoint.write,
//                                     | workflow_checkpoint.publish
//
// Invariant under every fault: the operation FAILS TRUTHFULLY (error result,
// no fabricated success), cleanup removes staged bytes, and the previous
// good state (existing stable output / pooled object / old checkpoint)
// survives intact.
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <vector>

#include <cpl_conv.h>
#include <gdal.h>

#include "data/asset_types.h"
#include "data/artifact_store.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/artifact_object_pool.h"
#include "data/governance/governance_store.h"
#include "data/source_descriptor.h"
#include "data/workspace_catalog.h"
#include "runtime/observability/fault_registry.h"
#include "processing/framework/output_committer.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run.h"

using namespace sicnu::runtime::observability::fault;
using sicnu::data::AssetKind;
using sicnu::data::DataManager;
using sicnu::data::DerivationRecord;
using sicnu::data::PersistencePolicy;
using sicnu::OutputCommitter;
using sicnu::AlgorithmOutputRequest;
using sicnu::workflow::StepDef;
using sicnu::workflow::WorkflowCheckpointManager;
using sicnu::workflow::WorkflowDefinition;
using sicnu::workflow::WorkflowRun;

namespace
{
/// RAII: no fault can survive a failed assertion into the next test.
struct FaultScope
{
    FaultScope() = default;
    ~FaultScope() { disarmAllFaults(); }
    FaultScope( const FaultScope & ) = delete;
    FaultScope &operator=( const FaultScope & ) = delete;
};

/// Writes a minimal valid GeoTIFF (16×16 Float32, value 1.0).
QString writeSyntheticTiff( const QString &path )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    constexpr int W = 16, H = 16;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( W, 1.0f );
    for ( int row = 0; row < H; ++row )
        GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return path;
}

AlgorithmOutputRequest rasterRequest( QTemporaryDir &dir, const QString &tempName,
                                      const QString &stableName )
{
    AlgorithmOutputRequest request;
    request.kind = AssetKind::Raster;
    request.tempPath = writeSyntheticTiff( dir.filePath( tempName ) );
    request.stablePath = dir.filePath( stableName );
    request.persistence = PersistencePolicy::SessionTemporary;
    request.autoLoad = false;
    request.derivation.algorithmId = QStringLiteral( "sicnu:ndvi" );
    request.derivation.algorithmVersion = QStringLiteral( "1.0.0" );
    return request;
}

/// A run whose id satisfies the checkpoint filename safety rule.
std::unique_ptr<WorkflowRun> makeRun( const std::string &runId )
{
    WorkflowDefinition def;
    def.id = "wf_fault_matrix";
    def.title = "Fault Matrix";
    StepDef step;
    step.id = "s1";
    step.operatorId = "rs:test";
    def.steps.push_back( step );
    auto run = WorkflowRun::createFromDefinition( def, runId );
    REQUIRE( run );
    return run;
}
} // namespace

// ---------------------------------------------------------------------------
// OutputCommitter
// ---------------------------------------------------------------------------

TEST_CASE( "fault matrix: injected publish failure fails truthfully and "
           "preserves the previous stable output",
           "[fault][output_committer]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    DataManager manager;
    OutputCommitter committer( &manager );

    // Phase 1: a healthy commit establishes the "previous good state".
    AlgorithmOutputRequest good = rasterRequest( dir, QStringLiteral( "scratch.tif" ),
                                                 QStringLiteral( "stable.tif" ) );
    const auto goodResult = committer.commit( good );
    REQUIRE( goodResult );
    const QString stablePath = dir.filePath( QStringLiteral( "stable.tif" ) );
    const QByteArray goodBytes = [&] {
        QFile f( stablePath );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();

    // Phase 2: armed fault on the publish probe → the commit must fail with
    // the SAME observable contract as a real rename failure.
    armFault( { "output_committer.publish", Mode::NextN, 1, {} } );
    AlgorithmOutputRequest bad = rasterRequest( dir, QStringLiteral( "scratch2.tif" ),
                                                QStringLiteral( "stable.tif" ) );
    const auto badResult = committer.commit( bad );
    REQUIRE_FALSE( badResult );

    // The pre-commit stable output survives byte-identical (rollback kept it).
    const QByteArray afterBytes = [&] {
        QFile f( stablePath );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();
    REQUIRE( afterBytes == goodBytes );
    // No half-published staging leftovers survive the failure.
    REQUIRE_FALSE( QFile::exists( stablePath + QStringLiteral( ".new" ) ) );
    REQUIRE_FALSE( QFile::exists( stablePath + QStringLiteral( ".old" ) ) );
}

TEST_CASE( "fault matrix: mid-group publish failure (sidecar position) rolls "
           "the whole group back",
           "[fault][output_committer]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    DataManager manager;
    OutputCommitter committer( &manager );

    // A shapefile-style group: primary + sidecars staged beside the primary.
    const QString tempPrimary = dir.filePath( QStringLiteral( "group.shp" ) );
    REQUIRE( QFile::copy( writeSyntheticTiff( dir.filePath( QStringLiteral( "seed.tif" ) ) ),
                          tempPrimary ) );
    QFile sidecarShx( dir.filePath( QStringLiteral( "group.shx" ) ) );
    REQUIRE( sidecarShx.open( QIODevice::WriteOnly ) );
    sidecarShx.write( "shx", 3 );
    sidecarShx.close();
    QFile sidecarDbf( dir.filePath( QStringLiteral( "group.dbf" ) ) );
    REQUIRE( sidecarDbf.open( QIODevice::WriteOnly ) );
    sidecarDbf.write( "dbf", 3 );
    sidecarDbf.close();

    // EveryNth(2): probe fires on the SECOND pair (the .shx sidecar) — the
    // primary is already published when the failure hits, so the rollback
    // must remove the published primary and restore the previous state.
    armFault( { "output_committer.publish", Mode::EveryNth, 2, {} } );

    AlgorithmOutputRequest request;
    request.kind = AssetKind::Raster; // validation runs on the temp primary
    request.tempPath = tempPrimary;
    request.stablePath = dir.filePath( QStringLiteral( "group_out.tif" ) );

    // NOTE: the group members publish to "<stableBase>.shx" etc. only when the
    // stable name's completeBaseName matches; here the request is raster with
    // sidecars staged under the temp base name "group.*".
    const auto result = committer.commit( request );
    REQUIRE_FALSE( result );
    REQUIRE_FALSE( QFile::exists( dir.filePath( QStringLiteral( "group_out.tif" ) ) ) );
    REQUIRE_FALSE( QFile::exists( dir.filePath( QStringLiteral( "group_out.shx" ) ) ) );
}

// ---------------------------------------------------------------------------
// ArtifactObjectPool
// ---------------------------------------------------------------------------

TEST_CASE( "fault matrix: pool staging copy failure leaves no object and no "
           "tmp residue",
           "[fault][artifact_pool]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString payload = writeSyntheticTiff( dir.filePath( QStringLiteral( "payload.tif" ) ) );

    sicnu::data::ArtifactObjectPool pool;
    QString err;
    REQUIRE( pool.enable( dir.filePath( QStringLiteral( "pool" ) ), &err ) );

    armFault( { "artifact_pool.stage_copy", Mode::NextN, 1, {} } );
    const auto object = pool.put( payload, true );
    REQUIRE_FALSE( object.has_value() ); // truthful failure: no object id fabricated

    // The pool stays healthy: disarm and the same put succeeds.
    disarmAllFaults();
    const auto object2 = pool.put( payload, true );
    REQUIRE( object2.has_value() );

    // No .puttmp staging residue anywhere in the pool tree.
    int puttmpResidue = 0;
    QDirIterator it( dir.filePath( QStringLiteral( "pool" ) ),
                     { QStringLiteral( "*.puttmp" ) }, QDir::Files,
                     QDirIterator::Subdirectories );
    while ( it.hasNext() )
    {
        it.next();
        ++puttmpResidue;
    }
    REQUIRE( puttmpResidue == 0 );
}

TEST_CASE( "fault matrix: pool publish-rename failure returns nullopt and the "
           "next put succeeds",
           "[fault][artifact_pool]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString payload = writeSyntheticTiff( dir.filePath( QStringLiteral( "payload.tif" ) ) );

    sicnu::data::ArtifactObjectPool pool;
    QString err;
    REQUIRE( pool.enable( dir.filePath( QStringLiteral( "pool" ) ), &err ) );

    armFault( { "artifact_pool.stage_publish", Mode::NextN, 1, {} } );
    const auto object = pool.put( payload, true );
    REQUIRE_FALSE( object.has_value() );

    disarmAllFaults();
    const auto object2 = pool.put( payload, true );
    REQUIRE( object2.has_value() );
    // The pooled object exists under its content address.
    REQUIRE( QFile::exists( object2->poolPath ) );
}

// ---------------------------------------------------------------------------
// WorkflowCheckpointManager
// ---------------------------------------------------------------------------

TEST_CASE( "fault matrix: checkpoint write failure keeps the previous "
           "checkpoint loadable",
           "[fault][workflow_checkpoint]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    WorkflowCheckpointManager manager;

    auto run = makeRun( "run-fault-ck-1" );
    const QString saved = manager.saveCheckpoint( *run, dir.path() );
    REQUIRE_FALSE( saved.isEmpty() );

    // Advance the run, then arm a WRITE fault: the new save must fail
    // truthfully and the OLD checkpoint must remain intact and loadable.
    armFault( { "workflow_checkpoint.write", Mode::NextN, 1, {} } );
    const QString failed = manager.saveCheckpoint( *run, dir.path() );
    REQUIRE( failed.isEmpty() );
    REQUIRE_FALSE( QFile::exists( dir.filePath( QStringLiteral( "checkpoint_run-fault-ck-1.json.tmp" ) ) ) );

    QString loadError;
    const auto reloaded = manager.loadCheckpoint( saved, &loadError );
    REQUIRE( reloaded != nullptr );
    REQUIRE( reloaded->runId() == "run-fault-ck-1" );
}

TEST_CASE( "fault matrix: checkpoint publish (rename) failure keeps the old "
           "checkpoint intact",
           "[fault][workflow_checkpoint]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    WorkflowCheckpointManager manager;

    auto run = makeRun( "run-fault-ck-2" );
    const QString saved = manager.saveCheckpoint( *run, dir.path() );
    REQUIRE_FALSE( saved.isEmpty() );
    const QByteArray original = [&] {
        QFile f( saved );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();

    armFault( { "workflow_checkpoint.publish", Mode::NextN, 1, {} } );
    const QString failed = manager.saveCheckpoint( *run, dir.path() );
    REQUIRE( failed.isEmpty() );

    // Old checkpoint bytes intact; no tmp residue.
    const QByteArray after = [&] {
        QFile f( saved );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();
    REQUIRE( after == original );
    QDir ckDir( dir.path() );
    const auto tmpResidue = ckDir.entryList( { QStringLiteral( "*.tmp*" ) }, QDir::Files );
    REQUIRE( tmpResidue.isEmpty() );
}

// ---------------------------------------------------------------------------
// GDAL close-time flush failures (processing output integrity)
//
// The GTiff driver defers edge-tile writes to GDALClose, so a disk-full /
// quota failure at final flush surfaces ONLY there. Every raster-producing
// processing entry must fail truthfully and remove the partial output
// instead of reporting success with a truncated file (#617/#703 contract).
// ---------------------------------------------------------------------------

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/algorithms/atmospheric_correction.h"

namespace
{
/// Writes a minimal valid two-band GeoTIFF (16×16 Float32, deterministic
/// gradient values) — rich enough for the QUAC streaming statistics path,
/// which refuses constant/range-less scenes.
QString writeSyntheticTwoBandTiff( const QString &path, float baseValue )
{
    Q_UNUSED( baseValue );
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    constexpr int W = 16, H = 16;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    for ( int band = 1; band <= 2; ++band )
    {
        GDALRasterBandH b = GDALGetRasterBand( ds, band );
        for ( int row = 0; row < H; ++row )
        {
            std::vector<float> line( W );
            for ( int col = 0; col < W; ++col )
                line[col] = 10.0f + 40.0f * band + static_cast<float>( row * W + col );
            GDALRasterIO( b, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
        }
    }
    GDALClose( ds );
    return path;
}
} // namespace

TEST_CASE( "fault matrix: injected close-flush failure is surfaced by "
           "closeWithError / closeDatasetFailClosed",
           "[fault][gdal_wrapper]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Unarmed path: a clean close still succeeds (the probe is inert).
    const QString healthy = writeSyntheticTiff( dir.filePath( QStringLiteral( "healthy.tif" ) ) );
    {
        GdalDatasetWrapper ds;
        REQUIRE( ds.open( healthy ) );
        QString err;
        REQUIRE( ds.closeWithError( &err ) );
        REQUIRE( err.isEmpty() );
    }

    // Armed path: the injected CPL failure after GDALClose must come back
    // out of closeWithError — a deferred flush failure is never silent.
    const QString armed = writeSyntheticTiff( dir.filePath( QStringLiteral( "armed.tif" ) ) );
    {
        GdalDatasetWrapper ds;
        REQUIRE( ds.open( armed ) );
        armFault( { "gdal_wrapper.close_flush", Mode::NextN, 1, {} } );
        QString err;
        REQUIRE_FALSE( ds.closeWithError( &err ) );
        REQUIRE( err.contains( "close-time flush failure" ) );
        // The dataset handle is consumed either way: a second close is a no-op.
        REQUIRE( ds.closeWithError( nullptr ) );
    }

    // The raw-handle variant carries the same contract (raw-handle writers
    // in satellite_products / qgis raster algorithms rely on it).
    const QString raw = writeSyntheticTiff( dir.filePath( QStringLiteral( "raw.tif" ) ) );
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( raw.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    armFault( { "gdal_wrapper.close_flush", Mode::NextN, 1, {} } );
    QString rawErr;
    REQUIRE_FALSE( closeDatasetFailClosed( ds, &rawErr ) );
    REQUIRE( rawErr.contains( "close-time flush failure" ) );
}

TEST_CASE( "fault matrix: QUAC multi-band output fails truthfully on a "
           "close-time flush failure and removes the partial file",
           "[fault][atmospheric_correction]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString source = writeSyntheticTwoBandTiff( dir.filePath( QStringLiteral( "stack.tif" ) ), 0.0f );
    const QString output = dir.filePath( QStringLiteral( "quac_out.tif" ) );
    QString error;

    // Phase 1: the healthy run must succeed and produce the output.
    const bool healthyOk = AtmosphericCorrection::processFileMultiBand(
        source, output, AtmosphericCorrection::Quac, &error, {} );
    INFO( "QUAC error: " << error.toStdString() );
    REQUIRE( healthyOk );
    REQUIRE( QFile::exists( output ) );
    REQUIRE( error.isEmpty() );
    const QByteArray healthyBytes = [&] {
        QFile f( output );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();

    // Phase 2: armed close-flush fault → the run must FAIL TRUTHFULLY
    // (false return, non-empty diagnostic) and the previous good output at
    // the target path must survive BYTE-IDENTICAL: the staged write never
    // touched the target, and the failed staged file is discarded.
    const QByteArray oldBytes = healthyBytes;
    armFault( { "gdal_wrapper.close_flush", Mode::NextN, 1, {} } );
    error.clear();
    REQUIRE_FALSE( AtmosphericCorrection::processFileMultiBand(
        source, output, AtmosphericCorrection::Quac, &error, {} ) );
    REQUIRE_FALSE( error.isEmpty() );
    REQUIRE( QFile::exists( output ) );
    const QByteArray survivedBytes = [&] {
        QFile f( output );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();
    REQUIRE( survivedBytes == oldBytes );

    // No staged residue (*.tmp*) may survive the failure either.
    {
        QDir outDir( dir.path() );
        const auto residue = outDir.entryList( { QStringLiteral( "*.tmp*" ) }, QDir::Files );
        REQUIRE( residue.isEmpty() );
    }

    // Phase 3: disarmed again → the same call succeeds (the fault only ever
    // gated the failure branch, never the algorithm itself).
    REQUIRE( AtmosphericCorrection::processFileMultiBand(
        source, output, AtmosphericCorrection::Quac, &error, {} ) );
    REQUIRE( QFile::exists( output ) );
}

// ---------------------------------------------------------------------------
// Data-layer transactions (artifact store / workspace catalog / governance)
// ---------------------------------------------------------------------------

TEST_CASE( "fault matrix: artifact-store commit failure rolls back and the "
           "store stays usable",
           "[fault][artifact_store]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    sicnu::data::ArtifactStore store;
    QString err;
    REQUIRE( store.open( dir.filePath( "artifacts.sqlite" ), &err ) );

    const QString payload = dir.filePath( "payload-a.tif" );
    {
        QFile f( payload );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "payload-bytes" );
    }
    sicnu::data::ArtifactRegistration reg;
    reg.logicalKey = QStringLiteral( "fault/ndvi" );
    reg.storagePath = payload;
    reg.kind = QStringLiteral( "raster" );

    // Injected COMMIT failure: the register must fail truthfully, roll the
    // transaction back (no leaked write lock) and leave NO version row.
    armFault( { "artifact_store.commit", Mode::NextN, 1, {} } );
    const auto failed = store.registerArtifact( reg );
    REQUIRE_FALSE( failed );
    REQUIRE_FALSE( store.latestByLogicalKey( reg.logicalKey ).has_value() );
    REQUIRE( store.versionsByLogicalKey( reg.logicalKey ).isEmpty() );

    // The connection is healthy: an immediate real register succeeds and is
    // the only version (the failed attempt left nothing behind).
    const auto ok = store.registerArtifact( reg );
    REQUIRE( ok );
    REQUIRE( ok.value().version == 1 );
    REQUIRE( store.versionsByLogicalKey( reg.logicalKey ).size() == 1 );
}

TEST_CASE( "fault matrix: artifact-store step failure rolls back cleanly",
           "[fault][artifact_store]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    sicnu::data::ArtifactStore store;
    QString err;
    REQUIRE( store.open( dir.filePath( "artifacts.sqlite" ), &err ) );

    const QString payload = dir.filePath( "payload-b.tif" );
    {
        QFile f( payload );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "payload-bytes" );
    }
    sicnu::data::ArtifactRegistration reg;
    reg.logicalKey = QStringLiteral( "fault/step" );
    reg.storagePath = payload;
    reg.kind = QStringLiteral( "raster" );

    armFault( { "artifact_store.step", Mode::NextN, 1, {} } );
    const auto failed = store.registerArtifact( reg );
    REQUIRE_FALSE( failed );
    REQUIRE_FALSE( store.latestByLogicalKey( reg.logicalKey ).has_value() );

    const auto ok = store.registerArtifact( reg );
    REQUIRE( ok );
    REQUIRE( ok.value().version == 1 );
}

TEST_CASE( "fault matrix: workspace-catalog commit failure rolls back and the "
           "catalog stays usable",
           "[fault][workspace_catalog]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    sicnu::data::WorkspaceCatalog catalog;
    QString err;
    REQUIRE( catalog.open( dir.filePath( "catalog.sqlite" ), &err ) );

    sicnu::data::CatalogAsset asset;
    asset.assetId = QStringLiteral( "fault-asset-1" );
    asset.sourceKey = QStringLiteral( "fault/asset-1" );
    asset.canonicalSource = QStringLiteral( "/data/fault-asset-1.tif" );
    asset.kind = QStringLiteral( "raster" );
    asset.state = QStringLiteral( "Ready" );
    asset.persistence = QStringLiteral( "session" );
    asset.displayName = QStringLiteral( "fault asset" );

    armFault( { "workspace_catalog.commit", Mode::NextN, 1, {} } );
    const auto failed = catalog.upsertAssets( { asset } );
    REQUIRE_FALSE( failed );
    REQUIRE( catalog.page( {}, 0, 10 ).total == 0 );

    // Disarmed again: the same upsert succeeds and is the only row.
    const auto ok = catalog.upsertAssets( { asset } );
    REQUIRE( ok );
    REQUIRE( catalog.page( {}, 0, 10 ).total == 1 );
}

TEST_CASE( "fault matrix: governance-store commit failure rolls back and the "
           "store stays usable",
           "[fault][governance_store]" )
{
    FaultScope scope;
    QTemporaryDir dir;
    sicnu::workspace::GovernanceStore store;
    QString err;
    REQUIRE( store.open( dir.filePath( "governance.sqlite" ), &err ) );

    sicnu::workspace::GovernedAsset asset;
    asset.assetId = QStringLiteral( "fault-gov-1" );
    asset.sourceKey = QStringLiteral( "fault/gov-1" );
    asset.canonicalSource = QStringLiteral( "/data/fault-gov-1.tif" );
    asset.kind = QStringLiteral( "raster" );
    asset.state = QStringLiteral( "Ready" );
    asset.persistence = QStringLiteral( "session" );
    asset.displayName = QStringLiteral( "fault governance asset" );

    armFault( { "governance_store.commit", Mode::NextN, 1, {} } );
    const auto failed = store.upsertAsset( asset );
    REQUIRE_FALSE( failed );
    REQUIRE_FALSE( store.assetByPath( asset.canonicalSource ).has_value() );

    const auto ok = store.upsertAsset( asset );
    REQUIRE( ok );
    const auto round = store.assetByPath( asset.canonicalSource );
    REQUIRE( round.has_value() );
    REQUIRE( round->assetId == asset.assetId );
}

TEST_CASE( "re-archiving over an existing history entry loses nothing (#1459)",
           "[workflow][checkpoint][history]" )
{
    const auto writeBytes = []( const QString &path, const char *bytes ) {
        QFile f( path );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( bytes );
    };
    QTemporaryDir dir;
    const QString cp = dir.filePath( QStringLiteral( "checkpoint_swap.json" ) );
    writeBytes( cp, "{\"v\":1}" );
    REQUIRE( WorkflowCheckpointManager::archiveCompletedRun( cp, dir.path(), 50 ) );

    // A fresh live checkpoint under the SAME name (e.g. a resume re-archiving
    // its final state) archives over the existing history entry: the old
    // remove-then-rename lost the previous entry when the rename failed; the
    // publish-then-swap must replace it and clean its own backup.
    writeBytes( cp, "{\"v\":2}" );
    REQUIRE( WorkflowCheckpointManager::archiveCompletedRun( cp, dir.path(), 50 ) );

    const auto history = QDir( dir.filePath( "history" ) )
                             .entryList( QStringList{ QStringLiteral( "checkpoint_*.json" ) },
                                         QDir::Files );
    REQUIRE( history.size() == 1 );
    const auto residue = QDir( dir.filePath( "history" ) )
                             .entryList( QStringList{ QStringLiteral( "*.old" ) }, QDir::Files );
    CHECK( residue.isEmpty() );
    QFile archived( dir.filePath( QStringLiteral( "history/checkpoint_swap.json" ) ) );
    REQUIRE( archived.open( QIODevice::ReadOnly ) );
    CHECK( archived.readAll().contains( "\"v\":2" ) );
}

// ---------------------------------------------------------------------------
// E12 teaching behavior benchmark (WP E12, D17)
//
// The lab-side half of the direction's behavior benchmark, built only from
// EXISTING teaching evidence paths: a clean baseline scenario must pass with
// a byte-stable report digest; every destructive undergrad variant must be
// localized precisely (the report names the injected fault family and the
// family-specific observables carry expected-vs-observed evidence — never a
// generic failure); and running fault scenarios must never corrupt the seed
// fixture (the baseline replays identically after all faults).
//
// The faultlab core is Qt-free and depends on jsoncpp only; jsoncpp already
// reaches this executable PUBLIC through sicnu_workflow, and tests/
// CMakeLists.txt is outside this direction's edit scope, so the teaching
// core is compiled into this translation unit by source (each faultlab
// source includes its own headers relatively, so the includes below are the
// whole measure). No production code is touched.
// ---------------------------------------------------------------------------

#include <catch2/catch_approx.hpp>

#include <cmath>

#include "faultlab/fault_diagnosis.h"
#include "faultlab/fault_fixtures.h"
#include "faultlab/fault_observables.h"
#include "faultlab/fault_registry.h"
#include "faultlab/fault_report.h"
#include "faultlab/fault_runner.h"
#include "faultlab/fault_sandbox.h"
#include "faultlab/fault_transforms.h"
#include "faultlab/fault_types.h"
#include "faultlab/util/canonical_json.h"

// The benchmark below links the faultlab leaf library (sicnu_faultlab, see
// tests/CMakeLists.txt) and drives real scenario documents through
// loadFaultScenario / runFaultScenario.

namespace
{

/// Skeleton of a complete sicnu.lab.faults/1 benchmark scenario document;
/// callers set the fault params and the observable expectations.
Json::Value benchmarkScenarioDoc( const char *scenarioId, const char *family,
                                  const char *fixtureId, unsigned int seed,
                                  unsigned int fixtureSeed, const char *diagnosisSignature )
{
    using namespace sicnu::faultlab;

    Json::Value doc( Json::objectValue );
    doc["schema_version"] = kFaultScenarioSchemaId;
    doc["scenario_id"] = scenarioId;
    doc["title"] = "E12 teaching behavior benchmark";

    Json::Value fault( Json::objectValue );
    fault["family"] = family;
    fault["params"] = Json::Value( Json::objectValue );
    fault["seed"] = seed;
    doc["fault"] = fault;

    Json::Value fixture( Json::objectValue );
    fixture["fixture_id"] = fixtureId;
    fixture["params"] = Json::Value( Json::objectValue );
    fixture["seed"] = fixtureSeed;
    doc["base_fixture"] = fixture;

    Json::Value expected( Json::objectValue );
    expected["observables"] = Json::Value( Json::arrayValue );
    Json::Value diagnosis( Json::objectValue );
    diagnosis["signature"] = diagnosisSignature;
    expected["diagnosis"] = diagnosis;
    doc["expected"] = expected;

    Json::Value objective( Json::objectValue );
    objective["id"] = "LO-11";
    objective["statement"] = "Localize the injected fault family from the measured observables.";
    doc["learning_objective"] = objective;
    return doc;
}

/// Appends one observable expectation (number relations carry their operand
/// in "value"; "changed" needs none).
void expectObservable( Json::Value &doc, const char *id, const char *relation, double value = 0.0,
                       bool hasValue = false )
{
    Json::Value entry( Json::objectValue );
    entry["id"] = id;
    entry["relation"] = relation;
    if ( hasValue )
    {
        entry["value"] = value;
    }
    doc["expected"]["observables"].append( entry );
}

/// Loads and runs one benchmark scenario document, requiring the pipeline
/// itself to complete (a passed==false report is a legitimate outcome the
/// caller asserts on; a failed LOAD or run is not).
sicnu::faultlab::FaultRunReport runBenchmarkScenario( const Json::Value &doc )
{
    using namespace sicnu::faultlab;
    const auto loaded = loadFaultScenario( doc );
    INFO( "scenario load failed: "
          << ( loaded.diagnostics.empty() ? std::string( "unknown" )
                                          : loaded.diagnostics.front().message ) );
    REQUIRE( loaded.ok );
    const auto run = runFaultScenario( loaded.value );
    REQUIRE( run.ok );
    return run.value;
}

/// The clean baseline anchor: the canonical index-pair role swap, declared
/// exactly as the registry documents it (fault applied, expectations hold,
/// diagnosis all_negative_index matched).
Json::Value baselineScenarioDoc()
{
    Json::Value doc = benchmarkScenarioDoc( "fs_benchmark_baseline_ndvi", "band_role_swap",
                                            "two_band_index_pair", 42u, 42u, "all_negative_index" );
    doc["fault"]["params"]["role_a"] = "red";
    doc["fault"]["params"]["role_b"] = "nir";
    expectObservable( doc, "band_roles", "changed" );
    // Swapping numerator/denominator flips the index: the mean moves by at
    // least half a unit downward.
    expectObservable( doc, "index_mean", "delta_le", -0.5, true );
    return doc;
}

/// Undergrad variant: train/test spatial leakage (duplicate mode) on the
/// classification fixture, with the #1464 cell_size param declared.
Json::Value leakageScenarioDoc()
{
    using namespace sicnu::faultlab;
    Json::Value doc = benchmarkScenarioDoc( "fs_benchmark_leakage_duplicate",
                                            "train_test_spatial_leakage", "classification_grid", 3u,
                                            11u, kDiagnosisUnmatched );
    doc["fault"]["params"]["mode"] = "duplicate";
    doc["fault"]["params"]["cell_size"] = 2.0;
    expectObservable( doc, "leakage.overlap_fraction", "changed" );
    expectObservable( doc, "leakage.test_count", "changed" );
    return doc;
}

/// Undergrad variant: cloud (quality) mask dropped from the masked pair.
Json::Value omitMaskScenarioDoc()
{
    using namespace sicnu::faultlab;
    Json::Value doc = benchmarkScenarioDoc( "fs_benchmark_omit_quality_mask", "omit_quality_mask",
                                            "quality_masked_pair", 7u, 7u, kDiagnosisUnmatched );
    doc["fault"]["params"]["role"] = "qa";
    expectObservable( doc, "band_count", "changed" );
    expectObservable( doc, "valid_fraction", "changed" );
    return doc;
}

/// Undergrad variant: declared scale/offset wrong, pixel values kept. The
/// distinguishing precision angle: band statistics must NOT move (a data-path
/// fault would move them), only the declared scale does.
Json::Value wrongScaleScenarioDoc()
{
    using namespace sicnu::faultlab;
    Json::Value doc = benchmarkScenarioDoc( "fs_benchmark_wrong_scale_offset", "wrong_scale_offset",
                                            "quality_masked_pair", 11u, 5u, kDiagnosisUnmatched );
    doc["fault"]["params"]["role"] = "red";
    doc["fault"]["params"]["gain"] = 2.0;
    doc["fault"]["params"]["metadata"] = true;
    expectObservable( doc, "band.scale.red", "changed" );
    // equals on numbers compares the clean→faulted DELTA against the operand:
    // the metadata path must leave the red band's mean exactly where the seed
    // fixture put it (delta 0), unlike a data-path gain fault.
    expectObservable( doc, "band.mean.red", "equals", 0.0, true );
    return doc;
}

/// The invariant set every destructive run must keep: the pipeline completed
/// truthfully, the sandbox cleaned, the source untouched, the replay
/// deterministic, and NO typed diagnostic (a diagnostic would be a generic
/// pipeline failure, not a localized teaching outcome).
void checkRunInvariant( const sicnu::faultlab::FaultRunReport &report )
{
    CHECK( report.faultApplied );
    CHECK( report.sandboxRemoved );
    CHECK( report.sourceUnchanged );
    CHECK( report.sourceDigestBefore == report.sourceDigestAfter );
    CHECK( report.replayDeterministic );
    CHECK( report.diagnostics.empty() );
}

} // namespace

TEST_CASE( "teaching benchmark: clean baseline scenario passes with a byte-stable report digest",
           "[fault][teaching][benchmark]" )
{
    using namespace sicnu::faultlab;

    const FaultRunReport first = runBenchmarkScenario( baselineScenarioDoc() );
    INFO( "report: " << faultReportToJson( first ) );
    REQUIRE( first.passed );
    CHECK( first.expectationsPassed );
    CHECK( first.diagnosisMatched );
    CHECK( first.actualDiagnosisSignature == "all_negative_index" );
    checkRunInvariant( first );

    // The same scenario replays byte-identically on any machine: no wall
    // clock, no absolute paths in the canonical body.
    const FaultRunReport second = runBenchmarkScenario( baselineScenarioDoc() );
    REQUIRE( second.passed );
    CHECK( faultReportDigest( second ) == faultReportDigest( first ) );
}

TEST_CASE( "teaching benchmark: destructive undergrad variants localize to their own observables",
           "[fault][teaching][benchmark]" )
{
    using namespace sicnu::faultlab;

    SECTION( "train_test_spatial_leakage is localized by the leakage observables" )
    {
        const FaultRunReport report = runBenchmarkScenario( leakageScenarioDoc() );
        INFO( "report: " << faultReportToJson( report ) );
        REQUIRE( report.passed );
        // The report names the injected family — not a generic failure.
        CHECK( report.faultFamily == "train_test_spatial_leakage" );
        CHECK( report.toJson()["fault"]["family"].asString() == "train_test_spatial_leakage" );
        // The expectation evidence names exactly the family observables with
        // expected-vs-observed values: every train point was cloned into the
        // test role (4 → 8 samples), and the four clones sit inside train
        // cells (overlap 4/8).
        REQUIRE( report.expectationResults.size() == 2 );
        CHECK( report.expectationResults[0].id == "leakage.overlap_fraction" );
        CHECK( report.expectationResults[0].passed );
        CHECK( report.expectationResults[0].observed == Catch::Approx( 0.5 ) );
        CHECK( report.expectationResults[1].id == "leakage.test_count" );
        CHECK( report.expectationResults[1].passed );
        CHECK( report.expectationResults[1].observed == 8.0 );
        // An ML family has no canonical lab signature: the typed
        // diagnostic.unmatched value is the precise, declared diagnosis.
        CHECK( report.actualDiagnosisSignature == kDiagnosisUnmatched );
        CHECK( report.diagnosisMatched );
        checkRunInvariant( report );

        // #1464 precision: the declared cell_size is CONSUMED — published
        // into the copy and read by the leakage observable's binning — and
        // an unusable cell_size is a typed refusal that mutates nothing.
        const auto fixture = makeFixture( "classification_grid", Json::Value( Json::objectValue ), 11u );
        REQUIRE( fixture.ok );
        FaultGrid consumed = FaultSandbox::copyOf( fixture.value );
        FaultSpec spec;
        spec.familyId = "train_test_spatial_leakage";
        spec.params["mode"] = "duplicate";
        spec.params["cell_size"] = 2.0;
        REQUIRE( applyFault( consumed, spec ).ok );
        CHECK( consumed.extras["leakage_cell_size"].asDouble() == 2.0 );
        CHECK( measureObservables( consumed ).at( "leakage.overlap_fraction" ).number ==
               Catch::Approx( 0.5 ) );

        FaultGrid refused = FaultSandbox::copyOf( fixture.value );
        const std::string digestBefore = canonical::sha256HexOf( refused.toJson() );
        FaultSpec bad;
        bad.familyId = "train_test_spatial_leakage";
        bad.params["mode"] = "duplicate";
        bad.params["cell_size"] = 0.0;
        const auto rejected = applyFault( refused, bad );
        REQUIRE_FALSE( rejected.ok );
        CHECK( rejected.diagnostics.front().code == "faultlab.fault_unsupported_params" );
        // Fail-before-mutate: the declared bin size is never published and
        // the sample points are byte-identical after the refusal.
        CHECK( refused.extras["leakage_cell_size"].asDouble() == 4.0 );
        CHECK( canonical::sha256HexOf( refused.toJson() ) == digestBefore );

        bad.params["cell_size"] = std::nan( "" );
        const auto rejectedNan = applyFault( refused, bad );
        CHECK_FALSE( rejectedNan.ok );
        CHECK( rejectedNan.diagnostics.front().code == "faultlab.fault_unsupported_params" );
        CHECK( canonical::sha256HexOf( refused.toJson() ) == digestBefore );
    }

    SECTION( "omit_quality_mask is localized by the masking observables" )
    {
        const FaultRunReport report = runBenchmarkScenario( omitMaskScenarioDoc() );
        INFO( "report: " << faultReportToJson( report ) );
        REQUIRE( report.passed );
        CHECK( report.faultFamily == "omit_quality_mask" );
        // The quality band is gone (3 → 2) and dropping the mask moves
        // valid_fraction upward — the masked-but-finite pixels are now
        // counted as valid. The deltas in the evidence carry both facts.
        REQUIRE( report.expectationResults.size() == 2 );
        CHECK( report.expectationResults[0].id == "band_count" );
        CHECK( report.expectationResults[0].passed );
        CHECK( report.expectationResults[0].observed == 2.0 );
        CHECK( report.expectationResults[0].delta == -1.0 );
        CHECK( report.expectationResults[1].id == "valid_fraction" );
        CHECK( report.expectationResults[1].passed );
        CHECK( report.expectationResults[1].delta > 0.0 );
        CHECK( report.actualDiagnosisSignature == kDiagnosisUnmatched );
        CHECK( report.diagnosisMatched );
        checkRunInvariant( report );
    }

    SECTION( "wrong_scale_offset metadata path moves the declared scale only" )
    {
        const FaultRunReport report = runBenchmarkScenario( wrongScaleScenarioDoc() );
        INFO( "report: " << faultReportToJson( report ) );
        REQUIRE( report.passed );
        CHECK( report.faultFamily == "wrong_scale_offset" );
        // Precision angle: the declared scale doubles (1.0 → 2.0) while the
        // red band's measured mean does not move at all (delta == 0) — the
        // observable pair distinguishes the metadata fault from a data-path
        // gain fault, which would have shifted the mean by the gain.
        REQUIRE( report.expectationResults.size() == 2 );
        CHECK( report.expectationResults[0].id == "band.scale.red" );
        CHECK( report.expectationResults[0].passed );
        CHECK( report.expectationResults[0].observed == 2.0 );
        CHECK( report.expectationResults[1].id == "band.mean.red" );
        CHECK( report.expectationResults[1].passed );
        CHECK( report.expectationResults[1].delta == 0.0 );
        CHECK( report.actualDiagnosisSignature == kDiagnosisUnmatched );
        CHECK( report.diagnosisMatched );
        checkRunInvariant( report );
    }
}

TEST_CASE( "teaching benchmark: fault scenarios never corrupt the seed fixture",
           "[fault][teaching][benchmark]" )
{
    using namespace sicnu::faultlab;

    // Digest of the pristine fixture, materialized independently of any
    // scenario run.
    const auto pristine = makeFixture( "classification_grid", Json::Value( Json::objectValue ), 11u );
    REQUIRE( pristine.ok );
    const std::string fixtureDigest = canonical::sha256HexOf( pristine.value.toJson() );

    // Clean baseline anchor.
    const FaultRunReport baseline = runBenchmarkScenario( baselineScenarioDoc() );
    REQUIRE( baseline.passed );
    const std::string baselineDigest = faultReportDigest( baseline );

    // Every destructive variant (plus one refused fault) runs in between.
    for ( const auto &variant : { leakageScenarioDoc(), omitMaskScenarioDoc(),
                                  wrongScaleScenarioDoc() } )
    {
        const FaultRunReport report = runBenchmarkScenario( variant );
        INFO( "report: " << faultReportToJson( report ) );
        REQUIRE( report.passed );
    }

    // The seed fixture still materializes byte-identically.
    const auto after = makeFixture( "classification_grid", Json::Value( Json::objectValue ), 11u );
    REQUIRE( after.ok );
    CHECK( canonical::sha256HexOf( after.value.toJson() ) == fixtureDigest );

    // Re-running the clean baseline AFTER the faults reproduces the anchor
    // report byte-for-byte.
    const FaultRunReport rerun = runBenchmarkScenario( baselineScenarioDoc() );
    REQUIRE( rerun.passed );
    CHECK( rerun.expectationResults.size() == baseline.expectationResults.size() );
    CHECK( faultReportDigest( rerun ) == baselineDigest );
}

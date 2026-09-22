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
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/artifact_object_pool.h"
#include "data/source_descriptor.h"
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

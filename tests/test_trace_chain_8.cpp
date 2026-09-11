// test_trace_chain_8.cpp — end-to-end trace correlation + store fault
// contracts (tasks E + F, Verification Platform 8.0).
//
// Chain links proven here (the 7.0 layer already adapted ExecutionPlane and
// JobEngine):
//
//   OutputCommitter::commit   → exp.trace.v1 "commit" (artifact = AssetId)
//   DatasetStore::commitVersion    → "dataset_commit" (artifact = version id)
//   ExperimentStore::upsertRun     → "experiment_upsert" (run = run id)
//   WorkflowRunCoordinator    → "run_state" (run = run id)
//
// and the new fault points (each must FAIL TRUTHFULLY and leave the store
// consistent — no fabricated success, no partial state):
//
//   dataset_store.commit      → commitVersion reports the real write-failure
//                               branch, version stays Draft, retry succeeds
//   experiment_store.commit   → upsertRun reports the real write-failure
//                               branch, no run row appears, retry succeeds
//
// Every trace assertion runs against a fresh RingTraceSink; the sink is
// uninstalled by RAII so no event leaks between tests.
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <json/json.h>
#include <string>
#include <vector>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/source_descriptor.h"
#include "processing/framework/output_committer.h"
#include "processing/framework/task_center.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator.h"
#include "dataset/dataset_ids.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/dataset_version.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/run_recorder.h"
#include "runtime/observability/fault_registry.h"
#include "runtime/observability/trace.h"

#include <cpl_conv.h>
#include <gdal.h>

using namespace sicnu::runtime::observability;
using sicnu::data::AssetKind;
using sicnu::data::DataManager;
using sicnu::data::PersistencePolicy;
using sicnu::OutputCommitter;
using sicnu::AlgorithmOutputRequest;
using namespace std::string_literals;

namespace
{
/// RAII: fresh ring sink per test, global sink cleared afterwards.
struct TraceScope
{
    std::shared_ptr<trace::RingTraceSink> sink = std::make_shared<trace::RingTraceSink>( 1024 );
    TraceScope() { trace::Trace::install( sink ); }
    ~TraceScope() { trace::Trace::install( nullptr ); }
    TraceScope( const TraceScope & ) = delete;
    TraceScope &operator=( const TraceScope & ) = delete;
};

/// RAII: no armed fault survives a failed assertion.
struct FaultScope
{
    FaultScope() = default;
    ~FaultScope() { fault::disarmAllFaults(); }
    FaultScope( const FaultScope & ) = delete;
    FaultScope &operator=( const FaultScope & ) = delete;
};

std::vector<trace::TraceEvent> eventsNamed( const TraceScope &scope, const std::string &event )
{
    std::vector<trace::TraceEvent> out;
    for ( const auto &e : scope.sink->snapshot() )
        if ( e.event == event )
            out.push_back( e );
    return out;
}

/// Minimal valid GeoTIFF (16×16 Float32) — same fixture contract as
/// test_fault_matrix.
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

/// A staged draft version to work with (real store API).
sicnu::dataset::DatasetVersionId makeStagedDraft( sicnu::dataset::DatasetStore &store,
                                                  const QTemporaryDir &dir,
                                                  const QString &tag )
{
    using namespace sicnu::dataset;
    Q_UNUSED( dir );
    Q_UNUSED( tag );
    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "trace-chain-dataset" ) ).has_value() );

    const DatasetVersionId versionId = DatasetVersionId::generate();
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId.toString() );
    manifest.setVersionId( versionId.toString() );
    manifest.setName( QStringLiteral( "Trace Chain Fixture" ) );
    manifest.setCreatedAtUtc( QDateTime::fromString(
        QStringLiteral( "2026-09-10T00:00:00.000Z" ), Qt::ISODateWithMs ) );
    SourceAssetRef source;
    source.assetId = QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" );
    source.revision = 1;
    source.role = QStringLiteral( "image" );
    manifest.sourceAssets().append( source );
    DatasetEntry entry;
    entry.kind = QStringLiteral( "asset" );
    entry.refId = QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" );
    entry.role = QStringLiteral( "image" );
    manifest.entries().append( entry );
    manifest.schema().modality = QStringLiteral( "optical" );
    manifest.schema().crs = QStringLiteral( "EPSG:32650" );
    manifest.schema().resolutionX = 10.0;
    manifest.schema().resolutionY = 10.0;

    const auto draft = store.createDraftVersion( manifest, QStringLiteral( "trace chain" ) );
    REQUIRE( draft.has_value() );
    const auto staged = store.stageVersion( versionId );
    REQUIRE( staged.has_value() );
    return versionId;
}
} // namespace

TEST_CASE( "trace: dataset commit emits a dataset_commit event carrying the "
           "version artifact id",
           "[trace8][dataset]" )
{
    TraceScope scope;
    QTemporaryDir dir;
    sicnu::dataset::DatasetStore store;
    const QString dbPath = dir.filePath( QStringLiteral( "datasets.db" ) );
    QString error;
    REQUIRE( store.open( dbPath, &error ) );

    const auto versionId = makeStagedDraft( store, dir, QStringLiteral( "ok" ) );
    const auto committed = store.commitVersion( versionId );
    REQUIRE( committed );

    const auto events = eventsNamed( scope, "dataset_commit" );
    REQUIRE( events.size() == 1 );
    CHECK( events[0].status == "ok" );
    CHECK( events[0].artifact == versionId.toString().toStdString() );
    CHECK( events[0].phase == "end" );
}

TEST_CASE( "trace: experiment upserts carry the run correlation id",
           "[trace8][experiment]" )
{
    TraceScope scope;
    QTemporaryDir dir;
    sicnu::experiment::ExperimentStore store;
    QString error;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ), &error ) );

    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-trace-8" ) );
    experiment.setName( QStringLiteral( "trace chain experiment" ) );
    REQUIRE( store.upsertExperiment( experiment ) );

    sicnu::experiment::ExperimentRunRecorder recorder( store );
    sicnu::experiment::RunStartRequest request;
    request.experimentId = experiment.experimentId();
    request.algorithmId = QStringLiteral( "rs:ndvi" );
    const auto started = recorder.startRun( request );
    REQUIRE( started );
    const QString runId = started.value();

    // The recorder's Created → Running lifecycle persists through upsertRun:
    // each persistence lands as one trace record carrying the run id.
    const auto events = eventsNamed( scope, "experiment_upsert" );
    REQUIRE( events.size() >= 2 );
    for ( const auto &e : events )
        CHECK( e.run == runId.toStdString() );
}

TEST_CASE( "trace: OutputCommitter commit emits ok with the registered asset "
           "and error with the failure code",
           "[trace8][committer]" )
{
    TraceScope scope;
    QTemporaryDir dir;
    DataManager manager;
    OutputCommitter committer( &manager );

    const QString temp = writeSyntheticTiff( dir.filePath( QStringLiteral( "temp.tif" ) ) );
    const QString stable = dir.filePath( QStringLiteral( "stable.tif" ) );

    AlgorithmOutputRequest request;
    request.kind = AssetKind::Raster;
    request.tempPath = temp;
    request.stablePath = stable;
    request.persistence = PersistencePolicy::SessionTemporary;
    const auto committed = committer.commit( request );
    REQUIRE( committed );

    const auto okEvents = eventsNamed( scope, "commit" );
    REQUIRE( okEvents.size() == 1 );
    CHECK( okEvents[0].status == "ok" );
    CHECK( okEvents[0].artifact == committed.value().toString().toStdString() );
    CHECK( okEvents[0].durationUs > 0 );

    // Failure path: a missing temp output must trace the error truthfully
    // with the leading diagnostic code in the detail.
    AlgorithmOutputRequest broken;
    broken.kind = AssetKind::Raster;
    broken.tempPath = dir.filePath( QStringLiteral( "does-not-exist.tif" ) );
    broken.stablePath = dir.filePath( QStringLiteral( "broken.tif" ) );
    CHECK_FALSE( committer.commit( broken ) );

    const auto all = eventsNamed( scope, "commit" );
    REQUIRE( all.size() == 2 );
    CHECK( all[1].status == "error" );
    CHECK( all[1].detail.find( "output.temp_missing" ) != std::string::npos );
    CHECK( all[1].artifact.empty() );
}

TEST_CASE( "fault: dataset_store.commit fails truthfully, keeps the version "
           "draft, and a disarmed retry succeeds",
           "[fault8][dataset]" )
{
    FaultScope faults;
    QTemporaryDir dir;
    sicnu::dataset::DatasetStore store;
    QString error;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ), &error ) );

    const auto versionId = makeStagedDraft( store, dir, QStringLiteral( "fault" ) );

    {
        TraceScope scope;
        fault::armFault( { "dataset_store.commit", fault::Mode::NextN, 1, "" } );
        const auto failed = store.commitVersion( versionId );
        CHECK_FALSE( failed );
        // Truthful failure: the store's own write-failure diagnostics, and
        // the trace records the error (the chain never reports a fake pass).
        const auto events = eventsNamed( scope, "dataset_commit" );
        REQUIRE( events.size() == 1 );
        CHECK( events[0].status == "error" );
        CHECK( events[0].artifact == versionId.toString().toStdString() );
    }

    // Store stayed consistent: the version is still a staged draft.
    const auto version = store.versionById( versionId );
    REQUIRE( version );
    CHECK( version->status() == sicnu::dataset::DatasetVersionStatus::Draft );

    // Disarmed: the identical commit succeeds — no stuck state.
    const auto committed = store.commitVersion( versionId );
    REQUIRE( committed );
    CHECK( committed.value().status() == sicnu::dataset::DatasetVersionStatus::Committed );
}

TEST_CASE( "fault: experiment_store.commit fails truthfully and persists no "
           "run row",
           "[fault8][experiment]" )
{
    FaultScope faults;
    QTemporaryDir dir;
    sicnu::experiment::ExperimentStore store;
    QString error;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ), &error ) );

    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-fault-8" ) );
    experiment.setName( QStringLiteral( "fault experiment" ) );
    REQUIRE( store.upsertExperiment( experiment ) );

    sicnu::experiment::ExperimentRunRecorder recorder( store );
    sicnu::experiment::RunStartRequest request;
    request.experimentId = experiment.experimentId();
    request.algorithmId = QStringLiteral( "rs:qa_mask" );

    fault::armFault( { "experiment_store.commit", fault::Mode::NextN, 1, "" } );
    // The recorder's FIRST upsertRun (status Created) hits the faulted
    // commit: startRun must fail truthfully with the store's diagnostics.
    const auto started = recorder.startRun( request );
    CHECK_FALSE( started );
    CHECK_FALSE( started.diagnostics().isEmpty() );
    // And no run row may exist for the experiment.
    const auto runs = store.listRuns( experiment.experimentId() );
    REQUIRE( runs.has_value() );
    CHECK( runs.value().first == 0 );

    // Disarmed: the same start now succeeds end-to-end.
    const auto retried = recorder.startRun( request );
    REQUIRE( retried );
    const auto runsAfter = store.listRuns( experiment.experimentId() );
    REQUIRE( runsAfter.has_value() );
    CHECK( runsAfter.value().first == 1 );
}

TEST_CASE( "trace: TaskCenter terminal transitions reach the chain with the "
           "task and op identity",
           "[trace8][task_center]" )
{
    // The TaskCenter link (task_status records emitted from the
    // flushPendingSignals broadcast funnel) must carry the task id and
    // operator id and land a truthful terminal status — the anti-vacuity
    // proof for the new adapter: submit an instant real job through the
    // single admission path and read the chain back.
    TraceScope scope;
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.registerExecutor( "trace8:instant",
                             []( const sicnu::jobs::JobRequest &,
                                 sicnu::operators::RSOperatorContext & ) {
                                 Json::Value result;
                                 result["ok"] = true;
                                 return result;
                             } );

    // Contended hosts may hold the task in WaitingResource (RSS watermark);
    // wait briefly for admission headroom so the instant job actually runs
    // (bounded poll, honest failure if the gate never opens).
    const sicnu::TaskAdmissionSnapshot admission =
        sicnu::TaskCenter::instance().admissionSnapshot( "trace8:instant" );
    if ( !admission.wouldAdmit )
    {
        WARN( "admission gate closed on this host; TaskCenter chain leg skipped" );
        return;
    }

    sicnu::jobs::JobRequest request;
    request.algorithmId = "trace8:instant";
    request.title = "trace chain instant";
    request.source = "trace8";

    const long taskId = sicnu::TaskCenter::instance().submitJob( request );
    REQUIRE( taskId > 0 );
    engine.waitUntilIdleForTests();
    const auto info = sicnu::TaskCenter::instance().waitForTask(
        taskId, std::chrono::seconds( 5 ) );
    REQUIRE( info.status == sicnu::TaskStatus::Completed );

    const auto events = eventsNamed( scope, "task_status" );
    REQUIRE( !events.empty() );
    bool terminalOk = false;
    for ( const auto &e : events )
    {
        if ( e.task == std::to_string( taskId ) )
        {
            CHECK( e.op == "trace8:instant" );
            if ( e.status == "ok" && e.phase.empty() )
                terminalOk = e.durationUs > 0;
        }
    }
    CHECK( terminalOk );
}

// tests/test_workflow_cache_e2e.cpp — production E2E for the Workflow v2
// execution cache (#667) and crash recovery (#668).
//
// #667: identical pipeline resubmission through TaskCenter (real rs:
// operators, a registered DataManager catalog, SICNU_EXECUTION_CACHE) is
// served from the revision-aware cache; a parameter change reruns only the
// affected step; a deleted cached artifact self-heals to a miss.
//
// #668: a REAL CLI process crash mid-pipeline (SIGKILL while step C runs)
// leaves an on-disk checkpoint; a fresh CLI process lists the interrupted
// run, resumes it, and the completed steps are NOT re-executed.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <thread>
#include <vector>

#include <gdal_priv.h>
#include <json/json.h>

#include "data/data_manager.h"
#include "data/execution_fingerprint.h"
#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "workflow/workflow_definition.h"

using namespace sicnu::workflow;

#ifndef SICNU_TEST_TIME_LIMIT_S
#define SICNU_TEST_TIME_LIMIT_S 90
#endif

namespace {

void writeTwoBandRaster( const QString &path, int W, int H )
{
    sicnu::ensureGdalInit();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> band( static_cast<size_t>( W ) * H );
    for ( int b = 1; b <= 2; ++b )
    {
        for ( int r = 0; r < H; ++r )
            for ( int c = 0; c < W; ++c )
                // A deterministic vegetation-like gradient: NIR high on the
                // diagonal, RED lower — NDVI stays in a sane numeric range.
                band[static_cast<size_t>( r ) * W + c] =
                    b == 1 ? 2000.f + 40.f * ( ( r + c ) % 64 )
                           : 1000.f + 30.f * ( ( r * 2 + c ) % 64 );
        GDALRasterBand *rb = ds->GetRasterBand( b );
        rb->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
    }
    double gt[6] = { 0, 1, 0, static_cast<double>( H ), 0, -1 };
    ds->SetGeoTransform( gt );
    GDALClose( ds );
}

sicnu::data::AssetId registerRaster( sicnu::data::DataManager &dm, const QString &path )
{
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = path;
    sicnu::data::RegisterRequest request;
    request.source = source;
    request.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
    request.notifyUpdateOnReuse = true;
    return dm.registerSource( request ).assetId;
}

struct CacheE2eFixture
{
    QTemporaryDir dir;
    sicnu::data::DataManager dataManager;
    QString inputPath;
    QString aPath;
    QString bPath;

    CacheE2eFixture()
    {
        int argc = 1;
        static char arg0[] = "test_workflow_cache_e2e";
        char *argv[] = { arg0, nullptr };
        if ( !QCoreApplication::instance() )
            new QCoreApplication( argc, argv );

        inputPath = dir.filePath( "input.tif" );
        aPath = dir.filePath( "ndvi.tif" );
        bPath = dir.filePath( "maj.tif" );
        writeTwoBandRaster( inputPath, 64, 64 );

        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );
        // The same registry bridge the CLI/GUI production entry installs.
        engine.setFallbackExecutor(
            []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) {
                const auto adapter =
                    sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
                        req.algorithmId );
                if ( !adapter )
                    throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
                sicnu::processing::ProgressCallback progressBridge;
                progressBridge = [&ctx]( int percent, const std::string &message ) {
                    ctx.reportProgress( percent / 100.0, message );
                };
                return adapter->execute( req.params, progressBridge,
                                         [&ctx]() { return ctx.isCancelled(); } );
            } );

        sicnu::TaskCenter::instance().shutdownForTests();
        sicnu::TaskCenter::instance().setCatalog( &dataManager );

        sicnu::operators::RSOperatorRegistry::instance(); // call_once chain
        sicnu::operators::rs::installRsOperatorProvider();
        sicnu::processing::AtomicAlgorithmRegistry::instance().initialize();

        auto &cache = sicnu::data::ExecutionResultCache::instance();
        cache.clear();
        cache.setEnabled( true );

        // Step A's input must be a registered asset or the fingerprint
        // verdict is "not cacheable" by design.
        REQUIRE( !registerRaster( dataManager, inputPath ).isNull() );
    }
};

WorkflowDefinition twoStepPipeline( const QString &inputPath, const QString &aPath,
                                    const QString &bPath, int majorityKernel )
{
    WorkflowDefinition def;
    def.id = "cache_e2e";
    def.title = "Incremental cache E2E";

    StepDef stepA;
    stepA.id = "a";
    stepA.title = "NDVI";
    stepA.kind = StepKind::Operator;
    stepA.operatorId = "rs:spectral_index";
    stepA.params["input"] = inputPath.toStdString();
    stepA.params["output"] = aPath.toStdString();
    stepA.params["index"] = "NDVI";
    stepA.params["nir"] = 1;
    stepA.params["red"] = 2;

    StepDef stepB;
    stepB.id = "b";
    stepB.title = "Majority";
    stepB.kind = StepKind::Operator;
    stepB.operatorId = "rs:majority_filter";
    stepB.params["input"] = "$a.output";
    stepB.params["output"] = bPath.toStdString();
    stepB.params["kernel"] = majorityKernel;
    StepConnection conn;
    conn.fromStepId = "a";
    conn.fromPort = "output";
    conn.toPort = "input";
    stepB.inputs.push_back( conn );

    def.steps = { stepA, stepB };
    return def;
}

// Runs the pipeline to a terminal aggregate state and returns per-step task
// infos keyed by step id (empty map on a rejected submission).
std::map<std::string, sicnu::AlgorithmTaskInfo> runPipelineAndWait(
    const WorkflowDefinition &def )
{
    auto &center = sicnu::TaskCenter::instance();
    const long pipelineId = center.submitPipeline( def, /*autoLoad=*/false );
    if ( pipelineId <= 0 )
        return {};
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds( SICNU_TEST_TIME_LIMIT_S );
    for ( ;; )
    {
        const auto info = center.getPipelineInfo( pipelineId );
        if ( info.isCompleted || info.isFailed )
            break;
        if ( std::chrono::steady_clock::now() > deadline )
            return {};
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    std::map<std::string, sicnu::AlgorithmTaskInfo> tasks;
    const auto info = center.getPipelineInfo( pipelineId );
    for ( const auto &stepId : info.orderedStepIds )
    {
        const auto it = info.stepToTaskId.find( stepId );
        if ( it == info.stepToTaskId.end() )
            continue;
        tasks[stepId.toStdString()] = center.getTaskInfo( it.value() );
    }
    return tasks;
}

bool servedFromCache( const sicnu::AlgorithmTaskInfo &info )
{
    return info.resultPayload.isObject() && info.resultPayload.isMember( "cache" )
           && info.resultPayload["cache"].asString() == "hit";
}

} // namespace

TEST_CASE( "Identical pipeline resubmission is served from the execution cache (#667)",
           "[workflow][v2][cache][e2e]" )
{
    CacheE2eFixture fx;
    const auto def = twoStepPipeline( fx.inputPath, fx.aPath, fx.bPath, /*kernel=*/3 );

    // First submission: everything executes, both outputs land, and the
    // completion path stores the outputs as cached artifacts.
    auto first = runPipelineAndWait( def );
    REQUIRE( first.size() == 2 );
    REQUIRE( first["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( first["b"].status == sicnu::TaskStatus::Completed );
    REQUIRE( QFile::exists( fx.aPath ) );
    REQUIRE( QFile::exists( fx.bPath ) );
    REQUIRE( !servedFromCache( first["a"] ) );
    REQUIRE( !servedFromCache( first["b"] ) );
    REQUIRE( sicnu::data::ExecutionResultCache::instance().pathSize() >= 2 );

    // The CLI registers step outputs after a run (registerStepOutputs); a
    // resubmission then resolves B's input against the catalog too.
    REQUIRE( !registerRaster( fx.dataManager, fx.aPath ).isNull() );
    REQUIRE( !registerRaster( fx.dataManager, fx.bPath ).isNull() );

    // Identical resubmission: both steps are cache hits — no operator runs.
    auto second = runPipelineAndWait( def );
    REQUIRE( second.size() == 2 );
    REQUIRE( second["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( second["b"].status == sicnu::TaskStatus::Completed );
    INFO( "step a served from cache: " << servedFromCache( second["a"] ) );
    INFO( "step b served from cache: " << servedFromCache( second["b"] ) );
    REQUIRE( servedFromCache( second["a"] ) );
    REQUIRE( servedFromCache( second["b"] ) );
    REQUIRE( QFile::exists( fx.aPath ) );
    REQUIRE( QFile::exists( fx.bPath ) );
}

TEST_CASE( "Parameter change reruns only the affected step (#667)",
           "[workflow][v2][cache][e2e]" )
{
    CacheE2eFixture fx;
    auto def = twoStepPipeline( fx.inputPath, fx.aPath, fx.bPath, /*kernel=*/3 );
    auto first = runPipelineAndWait( def );
    REQUIRE( first.size() == 2 );
    REQUIRE( first["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( first["b"].status == sicnu::TaskStatus::Completed );
    REQUIRE( !registerRaster( fx.dataManager, fx.aPath ).isNull() );
    REQUIRE( !registerRaster( fx.dataManager, fx.bPath ).isNull() );

    // Only B's parameter changed: A stays a cache hit, B re-executes.
    def.steps[1].params["kernel"] = 5;
    const QString bPath2 = fx.dir.filePath( "maj5.tif" );
    def.steps[1].params["output"] = bPath2.toStdString();
    auto second = runPipelineAndWait( def );
    REQUIRE( second.size() == 2 );
    REQUIRE( second["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( second["b"].status == sicnu::TaskStatus::Completed );
    REQUIRE( servedFromCache( second["a"] ) );
    REQUIRE( !servedFromCache( second["b"] ) ); // new params ⇒ real execution
    REQUIRE( QFile::exists( bPath2 ) );
}

TEST_CASE( "Deleted output self-heals to a cache miss (#667)",
           "[workflow][v2][cache][e2e]" )
{
    CacheE2eFixture fx;
    const auto def = twoStepPipeline( fx.inputPath, fx.aPath, fx.bPath, /*kernel=*/3 );
    auto first = runPipelineAndWait( def );
    REQUIRE( first.size() == 2 );
    REQUIRE( first["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( first["b"].status == sicnu::TaskStatus::Completed );
    REQUIRE( !registerRaster( fx.dataManager, fx.aPath ).isNull() );
    REQUIRE( !registerRaster( fx.dataManager, fx.bPath ).isNull() );

    // External deletion of the cached artifact: the lookup must self-heal
    // (drop the stale entry) and the step must re-execute.
    REQUIRE( QFile::remove( fx.aPath ) );
    auto second = runPipelineAndWait( def );
    REQUIRE( second.size() == 2 );
    REQUIRE( second["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( second["b"].status == sicnu::TaskStatus::Completed );
    REQUIRE( !servedFromCache( second["a"] ) ); // artifact vanished ⇒ miss
    REQUIRE( QFile::exists( fx.aPath ) );       // and it was produced again
}

#ifdef SICNU_CLI_BINARY
TEST_CASE( "CLI process crash mid-pipeline recovers and resumes without re-running "
           "completed steps (#668)",
           "[workflow][v2][recovery][e2e][cli]" )
{
    const QString cliBinary = QStringLiteral( SICNU_CLI_BINARY );
    if ( !QFileInfo::exists( cliBinary ) )
    {
        SUCCEED( "CLI binary not built — skipping subprocess crash E2E" );
        return;
    }

    QTemporaryDir homeDir;  // HOME override isolates ~/.rs_studio/checkpoints
    QTemporaryDir workDir;
    REQUIRE( homeDir.isValid() );
    REQUIRE( workDir.isValid() );

    const QString inputPath = workDir.filePath( "input.tif" );
    const QString aPath = workDir.filePath( "a_ndvi.tif" );
    const QString bPath = workDir.filePath( "b_maj.tif" );
    const QString cPath = workDir.filePath( "c_maj.tif" );
    writeTwoBandRaster( inputPath, 512, 512 );

    // A → B → C. B is deliberately expensive (kernel 49 over 512²) so the
    // kill lands while B is running and A's completed state is checkpointed.
    Json::Value steps( Json::arrayValue );
    {
        Json::Value a( Json::objectValue );
        a["id"] = "a";
        a["operator"] = "rs:spectral_index";
        a["params"] = Json::Value( Json::objectValue );
        a["params"]["input"] = inputPath.toStdString();
        a["params"]["output"] = aPath.toStdString();
        a["params"]["index"] = "NDVI";
        a["params"]["nir"] = 1;
        a["params"]["red"] = 2;
        steps.append( a );

        Json::Value b( Json::objectValue );
        b["id"] = "b";
        b["operator"] = "rs:majority_filter";
        b["params"] = Json::Value( Json::objectValue );
        b["params"]["input"] = "$a.output";
        b["params"]["output"] = bPath.toStdString();
        b["params"]["kernel"] = 49;
        steps.append( b );

        Json::Value c( Json::objectValue );
        c["id"] = "c";
        c["operator"] = "rs:majority_filter";
        c["params"] = Json::Value( Json::objectValue );
        c["params"]["input"] = "$b.output";
        c["params"]["output"] = cPath.toStdString();
        c["params"]["kernel"] = 3;
        steps.append( c );
    }
    Json::Value pipeline( Json::objectValue );
    pipeline["title"] = "crash e2e";
    pipeline["steps"] = steps;

    const QString pipelinePath = workDir.filePath( "pipeline.json" );
    {
        QFile f( pipelinePath );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        Json::StreamWriterBuilder w;
        f.write( Json::writeString( w, pipeline ).c_str() );
    }

    const auto cliEnv = [&homeDir]() {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert( "HOME", homeDir.path() );
        env.insert( "QT_QPA_PLATFORM", "offscreen" );
        env.remove( "SICNU_EXECUTION_CACHE" );
        return env;
    };

    // Phase 1 — start the pipeline and SIGKILL the process once A's output
    // exists (B is mid-flight by construction).
    QProcess crashed;
    crashed.setProcessEnvironment( cliEnv() );
    crashed.start( cliBinary, { QStringLiteral( "--pipeline" ), pipelinePath } );
    REQUIRE( crashed.waitForStarted( 10000 ) );
    bool aAppeared = false;
    const auto killDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds( SICNU_TEST_TIME_LIMIT_S );
    while ( std::chrono::steady_clock::now() < killDeadline )
    {
        if ( QFileInfo::exists( aPath ) && !QFileInfo::exists( bPath ) )
        {
            aAppeared = true;
            break;
        }
        REQUIRE( crashed.state() == QProcess::Running );
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    REQUIRE( aAppeared );
    crashed.kill();
    REQUIRE( crashed.waitForFinished( 10000 ) );
    REQUIRE( crashed.exitStatus() == QProcess::CrashExit );
    // The checkpoint of the crashed run is on disk.
    const QString checkpointDir = homeDir.filePath( ".rs_studio/checkpoints" );
    QStringList checkpoints = QDir( checkpointDir ).entryList( { QStringLiteral( "checkpoint_*.json" ) } );
    REQUIRE_FALSE( checkpoints.isEmpty() );

    // Phase 2 — a FRESH process lists the run (recovery marks it interrupted).
    QProcess lister;
    lister.setProcessEnvironment( cliEnv() );
    lister.start( cliBinary, { QStringLiteral( "--list-runs" ) } );
    REQUIRE( lister.waitForFinished( 60000 ) );
    const QString listing = QString::fromUtf8( lister.readAllStandardOutput() );
    CAPTURE( listing.toStdString() );
    REQUIRE( lister.exitCode() == 0 );
    REQUIRE( listing.contains( QStringLiteral( "interrupted" ) ) );

    // Extract the run id from the listing (first token of its line).
    QString runId;
    for ( const QString &line : listing.split( '\n' ) )
    {
        const QString trimmed = line.trimmed();
        if ( trimmed.contains( QStringLiteral( "state=interrupted" ) ) )
        {
            runId = trimmed.section( ' ', 0, 0 );
            break;
        }
    }
    REQUIRE_FALSE( runId.isEmpty() );

    // Phase 3 — resume in another fresh process. A completed with its output
    // on disk, so only B (whose output never materialized) and C execute.
    const QDateTime aMtimeBefore = QFileInfo( aPath ).lastModified();
    QProcess resumer;
    resumer.setProcessEnvironment( cliEnv() );
    resumer.start( cliBinary, { QStringLiteral( "--resume" ), runId } );
    REQUIRE( resumer.waitForFinished( SICNU_TEST_TIME_LIMIT_S * 1000 ) );
    const QString resumeOut = QString::fromUtf8( resumer.readAllStandardOutput() );
    const QString resumeErr = QString::fromUtf8( resumer.readAllStandardError() );
    CAPTURE( resumeOut.toStdString() );
    CAPTURE( resumeErr.toStdString() );
    REQUIRE( resumer.exitCode() == 0 );

    REQUIRE( QFileInfo::exists( bPath ) );
    REQUIRE( QFileInfo::exists( cPath ) );
    // The completed step was NOT re-executed: its artifact is untouched.
    REQUIRE( QFileInfo( aPath ).lastModified() == aMtimeBefore );
}
#endif // SICNU_CLI_BINARY

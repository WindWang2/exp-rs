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

#include <csignal>
#include <cmath>
#include <map>
#include <sstream>
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

/// Parsed statuses of the A→B live-owner window. JsonCpp serializes object
/// keys alphabetically (`std::map`), so `"status"` is written *before*
/// `"stepId"` in each StepPlan: a scan-forward from `"stepId":"a"` reads
/// step B's status and the live-owner marker never matches.
struct CheckpointStepWindow
{
    bool parseOk = false;
    std::string state;
    std::string aStatus;
    std::string bStatus;
};

CheckpointStepWindow parseCheckpointStepWindow( const QByteArray &text )
{
    CheckpointStepWindow out;
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::istringstream stream( text.toStdString() );
    if ( !Json::parseFromStream( builder, stream, &root, &errs ) )
        return out;
    out.parseOk = true;
    out.state = root["state"].asString();
    const Json::Value &plans = root["stepPlans"];
    if ( !plans.isArray() )
        return out;
    for ( Json::ArrayIndex i = 0; i < plans.size(); ++i )
    {
        const std::string id = plans[i]["stepId"].asString();
        const std::string status = plans[i]["status"].asString();
        if ( id == "a" )
            out.aStatus = status;
        else if ( id == "b" )
            out.bStatus = status;
    }
    return out;
}

bool isLiveOwnerCheckpointWindow( const CheckpointStepWindow &window )
{
    return window.parseOk && window.state == "Running" && window.aStatus == "Completed"
           && window.bStatus != "Completed";
}

/// SIGSTOP is asynchronous: the target may persist another checkpoint (e.g.
/// B Pending→Running) after kill() returns. Ownership assertions must wait
/// until /proc shows the process is actually stopped.
bool waitUntilProcessStopped( qint64 pid, std::chrono::milliseconds timeout )
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while ( std::chrono::steady_clock::now() < deadline )
    {
        QFile statFile( QStringLiteral( "/proc/%1/stat" ).arg( pid ) );
        if ( statFile.open( QIODevice::ReadOnly ) )
        {
            const QByteArray line = statFile.readAll();
            // /proc/<pid>/stat: pid (comm) state ... — comm may contain ')'
            const int rparen = line.lastIndexOf( ')' );
            if ( rparen >= 0 && rparen + 2 < line.size() )
            {
                const char state = static_cast<char>( line.at( rparen + 2 ) );
                if ( state == 'T' || state == 't' )
                    return true;
            }
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    return false;
}

void writeTwoBandRaster( const QString &path, int W, int H )
{    ::ensureGdalInit();
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
        tasks[stepId] = center.getTaskInfo( it.value() );
    }
    return tasks;
}

bool servedFromCache( const sicnu::AlgorithmTaskInfo &info )
{
    return info.resultPayload.isObject() && info.resultPayload.isMember( "cache" )
           && info.resultPayload["cache"].asString() == "hit";
}

/// Writes a dated single-band scene (SICNU_ACQUISITION_DATE metadata), the
/// input shape rs:temporal_composite consumes.
bool writeDatedScene( const QString &path, const QString &date, float value )
{
    ::ensureGdalInit();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), 4, 4, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    ds->SetMetadataItem( "SICNU_ACQUISITION_DATE", date.toUtf8().constData() );
    std::vector<float> band( 16, value );
    GDALRasterBand *rb = ds->GetRasterBand( 1 );
    REQUIRE( rb->RasterIO( GF_Write, 0, 0, 4, 4, band.data(), 4, 4, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
    return true;
}

} // namespace

// Production-faithful post-run registration (#726): the CLI registers every
// step output after EVERY pipeline run (registerStepOutputs), so a
// convergence test must model the loop, not register once and assume.
void registerPipelineOutputs( CacheE2eFixture &fx,
                              const std::map<std::string, sicnu::AlgorithmTaskInfo> &tasks )
{
    for ( const auto &[stepId, task] : tasks )
    {
        if ( task.status != sicnu::TaskStatus::Completed || task.outputLayerPath.isEmpty() )
            continue;
        REQUIRE( !registerRaster( fx.dataManager, task.outputLayerPath ).isNull() );
    }
}

bool hasExecutionFingerprint( const sicnu::AlgorithmTaskInfo &info )
{
    return info.resultPayload.isObject()
           && info.resultPayload.isMember( "executionFingerprint" )
           && info.resultPayload["executionFingerprint"].isString()
           && !info.resultPayload["executionFingerprint"].asString().empty();
}

TEST_CASE( "Identical pipeline resubmission converges to cache hits (#667, #726)",
           "[workflow][v2][cache][e2e]" )
{
    CacheE2eFixture fx;
    const auto def = twoStepPipeline( fx.inputPath, fx.aPath, fx.bPath, /*kernel=*/3 );

    // First submission: everything executes for real, and — the #726
    // contract — EVERY deterministic step acquires a usable cache identity
    // during the first cold run: B's input ($a.output) is keyed on A's
    // chained execution fingerprint, so worker-thread admission never needs
    // the catalog and B is cacheable from the first run on.
    auto first = runPipelineAndWait( def );
    REQUIRE( first.size() == 2 );
    REQUIRE( first["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( first["b"].status == sicnu::TaskStatus::Completed );
    REQUIRE( QFile::exists( fx.aPath ) );
    REQUIRE( QFile::exists( fx.bPath ) );
    REQUIRE( !servedFromCache( first["a"] ) );
    REQUIRE( !servedFromCache( first["b"] ) );
    REQUIRE( hasExecutionFingerprint( first["a"] ) );
    REQUIRE( hasExecutionFingerprint( first["b"] ) );
    REQUIRE( sicnu::data::ExecutionResultCache::instance().pathSize() >= 2 );
    registerPipelineOutputs( fx, first );

    // Second, identical submission: both steps are cache hits — no operator
    // runs at all. The re-registration above must NOT have invalidated them
    // (revision convergence: chained identities do not chase re-publications
    // of unchanged executions).
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
    registerPipelineOutputs( fx, second );

    // Third submission: the loop stays converged.
    auto third = runPipelineAndWait( def );
    REQUIRE( third.size() == 2 );
    REQUIRE( third["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( third["b"].status == sicnu::TaskStatus::Completed );
    REQUIRE( servedFromCache( third["a"] ) );
    REQUIRE( servedFromCache( third["b"] ) );
    registerPipelineOutputs( fx, third );
}

TEST_CASE( "Cold three-step pipeline fingerprints every step in one run (#726)",
           "[workflow][v2][cache][e2e]" )
{
    CacheE2eFixture fx;
    const QString cPath = fx.dir.filePath( "maj2.tif" );

    WorkflowDefinition def;
    def.id = "cache_e2e_cold_chain";
    def.title = "Cold chain cache identity";
    StepDef stepA;
    stepA.id = "a";
    stepA.kind = StepKind::Operator;
    stepA.operatorId = "rs:spectral_index";
    stepA.params["input"] = fx.inputPath.toStdString();
    stepA.params["output"] = fx.aPath.toStdString();
    stepA.params["index"] = "NDVI";
    stepA.params["nir"] = 1;
    stepA.params["red"] = 2;
    StepDef stepB;
    stepB.id = "b";
    stepB.kind = StepKind::Operator;
    stepB.operatorId = "rs:majority_filter";
    stepB.params["input"] = "$a.output";
    stepB.params["output"] = fx.bPath.toStdString();
    stepB.params["kernel"] = 3;
    StepConnection connB;
    connB.fromStepId = "a";
    connB.fromPort = "output";
    connB.toPort = "input";
    stepB.inputs.push_back( connB );
    StepDef stepC;
    stepC.id = "c";
    stepC.kind = StepKind::Operator;
    stepC.operatorId = "rs:majority_filter";
    stepC.params["input"] = "$b.output";
    stepC.params["output"] = cPath.toStdString();
    stepC.params["kernel"] = 3;
    StepConnection connC;
    connC.fromStepId = "b";
    connC.fromPort = "output";
    connC.toPort = "input";
    stepC.inputs.push_back( connC );
    def.steps = { stepA, stepB, stepC };

    // One cold submission: every deterministic step completes, stores a
    // usable cache identity and an execution fingerprint in its payload —
    // downstream steps included (no N-run warm-up).
    auto first = runPipelineAndWait( def );
    REQUIRE( first.size() == 3 );
    for ( const auto &stepId : { "a", "b", "c" } )
    {
        INFO( "step " << stepId );
        REQUIRE( first[stepId].status == sicnu::TaskStatus::Completed );
        REQUIRE( hasExecutionFingerprint( first[stepId] ) );
        REQUIRE( !servedFromCache( first[stepId] ) );
    }
    REQUIRE( sicnu::data::ExecutionResultCache::instance().pathSize() >= 3 );
    registerPipelineOutputs( fx, first );

    // The immediate resubmission is a full hit.
    auto second = runPipelineAndWait( def );
    REQUIRE( second.size() == 3 );
    for ( const auto &stepId : { "a", "b", "c" } )
    {
        INFO( "step " << stepId );
        REQUIRE( second[stepId].status == sicnu::TaskStatus::Completed );
        REQUIRE( servedFromCache( second[stepId] ) );
    }
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

TEST_CASE( "checkpoint step status is read from the owning StepPlan, not the next key after stepId",
           "[workflow][v2][recovery][ownership]" )
{
    // JsonCpp writes object members in std::map order, so "status" precedes
    // "stepId". The live-owner E2E must not scan forward from stepId.
    Json::Value planA( Json::objectValue );
    planA["stepId"] = "a";
    planA["status"] = "Completed";
    Json::Value planB( Json::objectValue );
    planB["stepId"] = "b";
    planB["status"] = "Running";
    Json::Value root( Json::objectValue );
    root["state"] = "Running";
    root["stepPlans"].append( planA );
    root["stepPlans"].append( planB );
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    const QByteArray text = QByteArray::fromStdString( Json::writeString( writer, root ) );
    REQUIRE( text.indexOf( "\"status\"" ) < text.indexOf( "\"stepId\"" ) );
    const auto parsed = parseCheckpointStepWindow( text );
    REQUIRE( parsed.parseOk );
    REQUIRE( parsed.state == "Running" );
    REQUIRE( parsed.aStatus == "Completed" );
    REQUIRE( parsed.bStatus == "Running" );
    REQUIRE( isLiveOwnerCheckpointWindow( parsed ) );
}

TEST_CASE( "In-place {input:x,output:x} and {input:y,output:y} never collide (#726)",
           "[workflow][v2][cache][e2e][identity]" )
{
    CacheE2eFixture fx;
    const QString xPath = fx.dir.filePath( "x_inplace.tif" );
    const QString yPath = fx.dir.filePath( "y_inplace.tif" );
    writeTwoBandRaster( xPath, 64, 64 );
    writeTwoBandRaster( yPath, 64, 64 );
    // Different bytes: y keeps a distinct diagonal so an NDVI over y differs.
    REQUIRE( !registerRaster( fx.dataManager, xPath ).isNull() );
    REQUIRE( !registerRaster( fx.dataManager, yPath ).isNull() );

    auto makeDef = []( const QString &input, const QString &output ) {
        WorkflowDefinition def;
        def.id = "cache_e2e_inplace";
        StepDef step;
        step.id = "a";
        step.kind = StepKind::Operator;
        step.operatorId = "rs:spectral_index";
        step.params["input"] = input.toStdString();
        step.params["output"] = output.toStdString();
        step.params["index"] = "NDVI";
        step.params["nir"] = 1;
        step.params["red"] = 2;
        def.steps = { step };
        return def;
    };

    // THE #726 collision regression: value-based destination exclusion erased
    // BOTH the input param and the input identity for in-place runs, hashing
    // {input:x, output:x} and {input:y, output:y} to the same digest and
    // letting the cache overwrite an undeclared file. The two runs must carry
    // distinct execution fingerprints and must not serve each other.
    auto runX = runPipelineAndWait( makeDef( xPath, xPath ) );
    REQUIRE( runX.size() == 1 );
    REQUIRE( runX["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( hasExecutionFingerprint( runX["a"] ) );
    const std::string fpX = runX["a"].resultPayload["executionFingerprint"].asString();
    registerPipelineOutputs( fx, runX );

    auto runY = runPipelineAndWait( makeDef( yPath, yPath ) );
    REQUIRE( runY.size() == 1 );
    REQUIRE( runY["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( hasExecutionFingerprint( runY["a"] ) );
    const std::string fpY = runY["a"].resultPayload["executionFingerprint"].asString();
    REQUIRE( fpX != fpY );
    REQUIRE( !servedFromCache( runY["a"] ) ); // y must never be served x's bytes
    registerPipelineOutputs( fx, runY );

    // Re-submitting the in-place run after the re-registration: x's revision
    // moved, so its fingerprint moved — the run must NOT be served from the
    // cache under the old identity (the safe direction; whether the operator
    // itself can re-read its own single-band NDVI output is not a cache
    // concern).
    auto rerunX = runPipelineAndWait( makeDef( xPath, xPath ) );
    REQUIRE( !servedFromCache( rerunX["a"] ) );
    if ( rerunX["a"].status == sicnu::TaskStatus::Completed )
        REQUIRE( hasExecutionFingerprint( rerunX["a"] ) );
}

TEST_CASE( "A cache hit materializes a changed destination and touches nothing else (#726)",
           "[workflow][v2][cache][e2e][safety]" )
{
    CacheE2eFixture fx;
    const QString aPath2 = fx.dir.filePath( "ndvi_copy.tif" );
    const QString sentinel = fx.dir.filePath( "sentinel_undeclared.tif" );
    writeTwoBandRaster( sentinel, 8, 8 );
    const QDateTime sentinelMtime = QFileInfo( sentinel ).lastModified();

    auto def = twoStepPipeline( fx.inputPath, fx.aPath, fx.bPath, /*kernel=*/3 );
    auto first = runPipelineAndWait( def );
    REQUIRE( first.size() == 2 );
    REQUIRE( first["a"].status == sicnu::TaskStatus::Completed );
    registerPipelineOutputs( fx, first );

    // Same science, different destination: destination is excluded from
    // identity by KEY semantics, so this is a hit — and the cached artifact
    // must be materialized ONTO the new destination transactionally.
    def.steps[0].params["output"] = aPath2.toStdString();
    auto second = runPipelineAndWait( def );
    REQUIRE( second.size() == 2 );
    REQUIRE( second["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( servedFromCache( second["a"] ) );
    REQUIRE( QFile::exists( aPath2 ) );                       // materialized
    REQUIRE( QFile::exists( fx.aPath ) );                     // original kept
    REQUIRE( second["a"].resultPayload["output"].asString() == aPath2.toStdString() );
    // The undeclared sentinel file was never touched by the serve.
    REQUIRE( QFileInfo( sentinel ).lastModified() == sentinelMtime );
}

TEST_CASE( "An input that cannot be identified never becomes cacheable (#726)",
           "[workflow][v2][cache][e2e][identity]" )
{
    CacheE2eFixture fx;
    // An existing local raster that is NOT registered in the catalog: no
    // provable data identity ⇒ no fingerprint ⇒ real execution, and nothing
    // may be stored.
    const QString unregistered = fx.dir.filePath( "unregistered_input.tif" );
    writeTwoBandRaster( unregistered, 32, 32 );

    WorkflowDefinition def;
    def.id = "cache_e2e_unidentifiable";
    StepDef step;
    step.id = "a";
    step.kind = StepKind::Operator;
    step.operatorId = "rs:spectral_index";
    step.params["input"] = unregistered.toStdString();
    step.params["output"] = fx.aPath.toStdString();
    step.params["index"] = "NDVI";
    step.params["nir"] = 1;
    step.params["red"] = 2;
    def.steps = { step };

    const int entriesBefore = sicnu::data::ExecutionResultCache::instance().pathSize();
    auto first = runPipelineAndWait( def );
    REQUIRE( first.size() == 1 );
    REQUIRE( first["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( !hasExecutionFingerprint( first["a"] ) );
    REQUIRE( !servedFromCache( first["a"] ) );
    REQUIRE( sicnu::data::ExecutionResultCache::instance().pathSize() == entriesBefore );

    // Even after its output is registered, the unidentifiable INPUT keeps the
    // step uncacheable — the conservative verdict must not erode.
    REQUIRE( !registerRaster( fx.dataManager, fx.aPath ).isNull() );
    auto second = runPipelineAndWait( def );
    REQUIRE( second["a"].status == sicnu::TaskStatus::Completed );
    REQUIRE( !servedFromCache( second["a"] ) );
    REQUIRE( sicnu::data::ExecutionResultCache::instance().pathSize() == entriesBefore );
}

TEST_CASE( "Grouped temporal composite hits restore the full multi-output payload (#726)",
           "[workflow][v2][cache][e2e][multioutput]" )
{
    CacheE2eFixture fx;
    const QString m1 = fx.dir.filePath( "scene_2025-01.tif" );
    const QString m2 = fx.dir.filePath( "scene_2025-02.tif" );
    REQUIRE( writeDatedScene( m1, QStringLiteral( "2025-01-15" ), 1.0f ) );
    REQUIRE( writeDatedScene( m2, QStringLiteral( "2025-02-10" ), 2.0f ) );
    REQUIRE( !registerRaster( fx.dataManager, m1 ).isNull() );
    REQUIRE( !registerRaster( fx.dataManager, m2 ).isNull() );

    const QString base = fx.dir.filePath( "per_month.tif" );
    WorkflowDefinition def;
    def.id = "cache_e2e_grouped";
    StepDef step;
    step.id = "comp";
    step.kind = StepKind::Operator;
    step.operatorId = "rs:temporal_composite";
    Json::Value scenes( Json::arrayValue );
    scenes.append( m1.toStdString() );
    scenes.append( m2.toStdString() );
    step.params["scenes"] = scenes;
    step.params["band"] = 1;
    step.params["period"] = "month";
    step.params["output"] = base.toStdString();
    def.steps = { step };

    auto first = runPipelineAndWait( def );
    REQUIRE( first.size() == 1 );
    REQUIRE( first["comp"].status == sicnu::TaskStatus::Completed );
    REQUIRE( hasExecutionFingerprint( first["comp"] ) );
    REQUIRE( first["comp"].resultPayload.isMember( "outputs" ) );
    REQUIRE( first["comp"].resultPayload["outputs"].size() == 2 );
    // The operator never writes the bare declared path in grouped mode; the
    // cache entry must still be valid (payload-artifact based, #726).
    REQUIRE( !QFile::exists( base ) );
    const int entriesAfterRun1 =
        sicnu::data::ExecutionResultCache::instance().pathSize();
    REQUIRE( entriesAfterRun1 >= 1 );

    auto second = runPipelineAndWait( def );
    REQUIRE( second.size() == 1 );
    REQUIRE( second["comp"].status == sicnu::TaskStatus::Completed );
    INFO( "grouped composite served from cache: " << servedFromCache( second["comp"] ) );
    REQUIRE( servedFromCache( second["comp"] ) );
    // The restored payload is the real-run payload shape: every period raster
    // present under outputs[] (GUI auto-load / agents cannot tell a hit).
    REQUIRE( second["comp"].resultPayload.isMember( "outputs" ) );
    REQUIRE( second["comp"].resultPayload["outputs"].size() == 2 );
    for ( const auto &period : first["comp"].resultPayload["outputs"] )
    {
        const std::string produced = period["output"].asString();
        REQUIRE( QFile::exists( QString::fromStdString( produced ) ) );
        bool found = false;
        for ( const auto &served : second["comp"].resultPayload["outputs"] )
            found |= served["output"].asString() == produced;
        REQUIRE( found );
    }
    REQUIRE( second["comp"].resultPayload["periodCount"].asInt() == 2 );
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
    // Kill only once step A's completion is CHECKPOINTED (not merely when its
    // output file appears): the fold+persist of the completion transition
    // lands milliseconds after the file write, and a kill inside that window
    // legitimately leaves A non-completed — resume would then re-run A. The
    // E2E asserts the stronger contract: a checkpointed-completed step never
    // re-executes.
    const QString checkpointDirEarly = homeDir.filePath( ".rs_studio/checkpoints" );
    bool aCheckpointed = false;
    const auto killDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds( SICNU_TEST_TIME_LIMIT_S );
    while ( std::chrono::steady_clock::now() < killDeadline )
    {
        if ( QFileInfo::exists( aPath ) )
        {
            const QStringList files = QDir( checkpointDirEarly )
                .entryList( { QStringLiteral( "checkpoint_*.json" ) } );
            for ( const QString &file : files )
            {
                QFile f( QDir( checkpointDirEarly ).filePath( file ) );
                if ( !f.open( QIODevice::ReadOnly ) )
                    continue;
                const QByteArray text = f.readAll();
                // StepPlan::toJson: {"stepId": "a", ..., "status": "Completed"}
                if ( text.contains( "\"stepId\" : \"a\"" )
                     && text.contains( "\"status\" : \"Completed\"" ) )
                {
                    aCheckpointed = true;
                    break;
                }
            }
        }
        if ( aCheckpointed )
            break;
        REQUIRE( crashed.state() == QProcess::Running );
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    REQUIRE( aCheckpointed );
    crashed.kill();
    REQUIRE( crashed.waitForFinished( 10000 ) );
    REQUIRE( crashed.exitStatus() == QProcess::CrashExit );
    // The checkpoint of the crashed run is on disk.
    const QString checkpointDir = homeDir.filePath( ".rs_studio/checkpoints" );
    QStringList checkpoints = QDir( checkpointDir ).entryList( { QStringLiteral( "checkpoint_*.json" ) } );
    REQUIRE_FALSE( checkpoints.isEmpty() );

    // Phase 2 — a FRESH process lists the run. The listing is strictly
    // read-only (#727): the stale owner is DISPLAYED as interrupted; the
    // on-disk checkpoint is reconciled only under the lock on resume.
    const QString crashedCheckpoint = QDir( checkpointDir ).filePath( checkpoints.front() );
    QFile cpBefore( crashedCheckpoint );
    REQUIRE( cpBefore.open( QIODevice::ReadOnly ) );
    const QByteArray cpBytesBefore = cpBefore.readAll();
    cpBefore.close();

    QProcess lister;
    lister.setProcessEnvironment( cliEnv() );
    lister.start( cliBinary, { QStringLiteral( "--list-runs" ) } );
    REQUIRE( lister.waitForFinished( 60000 ) );
    const QString listing = QString::fromUtf8( lister.readAllStandardOutput() );
    CAPTURE( listing.toStdString() );
    REQUIRE( lister.exitCode() == 0 );
    REQUIRE( listing.contains( QStringLiteral( "interrupted" ), Qt::CaseInsensitive ) );

    QFile cpAfter( crashedCheckpoint );
    REQUIRE( cpAfter.open( QIODevice::ReadOnly ) );
    REQUIRE( cpAfter.readAll() == cpBytesBefore ); // listing did not mutate
    cpAfter.close();

    // Extract the run id from the listing (first token of its line).
    QString runId;
    for ( const QString &line : listing.split( '\n' ) )
    {
        const QString trimmed = line.trimmed();
        if ( trimmed.toLower().contains( QStringLiteral( "state=interrupted" ) ) )
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
    // The pre-crash completed step's output was REGISTERED by the resuming
    // process (registerOutputAsset on checkpoint-served plans — adversarial
    // review of #724): without it, a resumed run's pre-crash outputs never
    // became assets and downstream lineage was permanently unresolved. The
    // DataManager lives in the child process, so we assert on the three
    // per-step registration log lines (a: checkpoint-served, b/c: fresh).
    int registeredOutputs = 0;
    for ( const QString &line : resumeOut.split( '\n' ) )
        if ( line.contains( QStringLiteral( "Registered step output asset" ) ) )
            ++registeredOutputs;
    REQUIRE( registeredOutputs == 3 );
    REQUIRE( resumeOut.contains( QStringLiteral( "Asset registration failed" ) ) == false );
}

TEST_CASE( "Cross-process ownership: --list-runs is read-only and --resume "
           "refuses a live owner (#727)",
           "[workflow][v2][recovery][ownership][e2e][cli]" )
{
    const QString cliBinary = QStringLiteral( SICNU_CLI_BINARY );
    if ( !QFileInfo::exists( cliBinary ) )
    {
        SUCCEED( "CLI binary not built — skipping subprocess ownership E2E" );
        return;
    }

    QTemporaryDir homeDir; // HOME override isolates ~/.rs_studio/checkpoints
    QTemporaryDir workDir;
    REQUIRE( homeDir.isValid() );
    REQUIRE( workDir.isValid() );

    const QString inputPath = workDir.filePath( "input.tif" );
    const QString aPath = workDir.filePath( "a_ndvi.tif" );
    const QString bPath = workDir.filePath( "b_maj.tif" );
    const QString cPath = workDir.filePath( "c_maj.tif" );
    // Same 512² raster as the crash E2E. SIGSTOP freezes P1 once A's
    // completion is checkpointed, so the list/resume window does not depend
    // on B remaining in-flight. Kernel 49 only has to keep B from finishing
    // between that checkpoint and the next poll (ASan cannot finish 2048²
    // NDVI — let alone majority-49 — inside SICNU_TEST_TIME_LIMIT_S).
    writeTwoBandRaster( inputPath, 512, 512 );

    // A → B → C. SIGSTOP right after A's completion is checkpointed freezes
    // the live owner for the list/resume assertions.
    Json::Value steps( Json::arrayValue );
    const auto addStep = [&]( const char *id, const char *op, const Json::Value &params ) {
        Json::Value s( Json::objectValue );
        s["id"] = id;
        s["operator"] = op;
        s["params"] = params;
        steps.append( s );
    };
    {
        Json::Value p( Json::objectValue );
        p["input"] = inputPath.toStdString();
        p["output"] = aPath.toStdString();
        p["index"] = "NDVI";
        p["nir"] = 1;
        p["red"] = 2;
        addStep( "a", "rs:spectral_index", p );
    }
    {
        Json::Value p( Json::objectValue );
        p["input"] = "$a.output";
        p["output"] = bPath.toStdString();
        p["kernel"] = 49;
        addStep( "b", "rs:majority_filter", p );
    }
    {
        Json::Value p( Json::objectValue );
        p["input"] = "$b.output";
        p["output"] = cPath.toStdString();
        p["kernel"] = 3;
        addStep( "c", "rs:majority_filter", p );
    }
    Json::Value pipeline( Json::objectValue );
    pipeline["title"] = "ownership e2e";
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

    const QString checkpointDir = homeDir.filePath( ".rs_studio/checkpoints" );

    // Phase 1 — P1 starts the run. Deterministic marker: the checkpoint shows
    // step "a" checkpointed-completed while "b" has not finished yet; P1 is
    // then SIGSTOPed. A stopped process is ALIVE (its open descriptors keep
    // the run lock held) but makes no progress, so the ownership assertions
    // below race against nothing.
    QProcess p1;
    p1.setProcessEnvironment( cliEnv() );
    p1.start( cliBinary, { QStringLiteral( "--pipeline" ), pipelinePath } );
    REQUIRE( p1.waitForStarted( 10000 ) );

    QString checkpointFile;
    QByteArray checkpointBefore;
    const auto markerDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds( SICNU_TEST_TIME_LIMIT_S );
    while ( std::chrono::steady_clock::now() < markerDeadline )
    {
        if ( p1.state() != QProcess::Running )
            FAIL( "P1 exited before its run checkpoint appeared" );
        const QStringList files = QDir( checkpointDir ).entryList(
            { QStringLiteral( "checkpoint_*.json" ) } );
        for ( const QString &file : files )
        {
            const QString full = QDir( checkpointDir ).filePath( file );
            QFile f( full );
            if ( !f.open( QIODevice::ReadOnly ) )
                continue;
            const QByteArray text = f.readAll();
            // Marker: step "a" checkpointed-Completed while "b" has not
            // finished, run still Running. Parsed from stepPlans (jsoncpp
            // emits "status" before "stepId"; a scan-forward from stepId
            // would attribute B's status to A).
            if ( isLiveOwnerCheckpointWindow( parseCheckpointStepWindow( text ) ) )
            {
                checkpointFile = full;
                break;
            }
        }
        if ( !checkpointFile.isEmpty() )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    INFO( "checkpoint: " << checkpointFile.toStdString() );
    if ( checkpointFile.isEmpty() )
    {
        INFO( "p1 stderr: " << p1.readAllStandardError().toStdString() );
        INFO( "p1 stdout: " << p1.readAllStandardOutput().toStdString() );
        const QStringList seen = QDir( checkpointDir ).entryList( QDir::Files );
        INFO( "checkpoint dir files: " << seen.join( QLatin1Char( ',' ) ).toStdString() );
        for ( const QString &file : seen )
        {
            if ( !file.endsWith( QStringLiteral( ".json" ) ) )
                continue;
            QFile f( QDir( checkpointDir ).filePath( file ) );
            if ( !f.open( QIODevice::ReadOnly ) )
                continue;
            const auto parsed = parseCheckpointStepWindow( f.readAll() );
            INFO( file.toStdString() << " parseOk=" << parsed.parseOk
                                     << " state=" << parsed.state
                                     << " a=" << parsed.aStatus
                                     << " b=" << parsed.bStatus );
        }
    }
    REQUIRE_FALSE( checkpointFile.isEmpty() );

    // Freeze P1 BEFORE snapshotting: B's Pending→Running persist lands
    // milliseconds after A's completion and would otherwise rewrite the
    // file between the marker read and SIGSTOP, falsely blaming --list-runs.
    const qint64 p1Pid = p1.processId();
    REQUIRE( p1Pid > 0 );
    REQUIRE( ::kill( p1Pid, SIGSTOP ) == 0 );
    REQUIRE( waitUntilProcessStopped( p1Pid, std::chrono::seconds( 5 ) ) );
    {
        QFile frozen( checkpointFile );
        REQUIRE( frozen.open( QIODevice::ReadOnly ) );
        checkpointBefore = frozen.readAll();
    }
    REQUIRE( isLiveOwnerCheckpointWindow( parseCheckpointStepWindow( checkpointBefore ) ) );

    const QString runId = QFileInfo( checkpointFile ).fileName().mid(
        QStringLiteral( "checkpoint_" ).size(),
        QFileInfo( checkpointFile ).fileName().size()
            - QStringLiteral( "checkpoint_" ).size()
            - QStringLiteral( ".json" ).size() );
    REQUIRE_FALSE( runId.isEmpty() );

    // Phase 2 — P2 --list-runs must be strictly READ-ONLY: the live run is
    // shown running (with its owner pid) and its checkpoint bytes are never
    // rewritten to Interrupted by a listing.
    QProcess lister;
    lister.setProcessEnvironment( cliEnv() );
    lister.start( cliBinary, { QStringLiteral( "--list-runs" ) } );
    REQUIRE( lister.waitForFinished( 60000 ) );
    const QString listing = QString::fromUtf8( lister.readAllStandardOutput() );
    CAPTURE( listing.toStdString() );
    REQUIRE( lister.exitCode() == 0 );
    REQUIRE( listing.contains( runId ) );
    REQUIRE( listing.contains( QStringLiteral( "(alive)" ) ) );
    REQUIRE_FALSE( listing.contains( QStringLiteral( "interrupted" ), Qt::CaseInsensitive ) );

    QFile afterList( checkpointFile );
    REQUIRE( afterList.open( QIODevice::ReadOnly ) );
    const QByteArray afterListBytes = afterList.readAll();
    afterList.close();
    INFO( "checkpoint bytes before=" << checkpointBefore.size()
                                     << " after list=" << afterListBytes.size() );
    REQUIRE( afterListBytes == checkpointBefore );

    // Phase 3 — P3 --resume of the LIVE run is refused; P1 is unaffected.
    QProcess resumer;
    resumer.setProcessEnvironment( cliEnv() );
    resumer.start( cliBinary, { QStringLiteral( "--resume" ), runId } );
    REQUIRE( resumer.waitForFinished( SICNU_TEST_TIME_LIMIT_S * 1000 ) );
    const QString resumeErr = QString::fromUtf8( resumer.readAllStandardError() );
    CAPTURE( resumeErr.toStdString() );
    REQUIRE( resumer.exitCode() != 0 );
    REQUIRE( ( resumeErr.contains( QStringLiteral( "Running" ) )
               || resumeErr.contains( QStringLiteral( "owned" ) ) ) );
    REQUIRE( p1.state() != QProcess::NotRunning ); // P1 (stopped) still owns the run

    QFile afterRefusedResume( checkpointFile );
    REQUIRE( afterRefusedResume.open( QIODevice::ReadOnly ) );
    const QByteArray afterResumeBytes = afterRefusedResume.readAll();
    afterRefusedResume.close();
    REQUIRE( afterResumeBytes == checkpointBefore );

    // Phase 4 — P1 (stopped) is SIGKILLed: the lock drops with the process,
    // the run becomes stale, and a fresh process may resume it.
    const QDateTime aMtimeBefore = QFileInfo( aPath ).lastModified();
    p1.kill();
    REQUIRE( p1.waitForFinished( 10000 ) );
    REQUIRE( p1.exitStatus() == QProcess::CrashExit );

    QProcess rescuer;
    rescuer.setProcessEnvironment( cliEnv() );
    rescuer.start( cliBinary, { QStringLiteral( "--resume" ), runId } );
    REQUIRE( rescuer.waitForFinished( SICNU_TEST_TIME_LIMIT_S * 1000 ) );
    const QString rescueOut = QString::fromUtf8( rescuer.readAllStandardOutput() );
    const QString rescueErr = QString::fromUtf8( rescuer.readAllStandardError() );
    CAPTURE( rescueOut.toStdString() );
    CAPTURE( rescueErr.toStdString() );
    REQUIRE( rescuer.exitCode() == 0 );
    REQUIRE( QFileInfo::exists( cPath ) );
    // The pre-crash completed step was NOT re-executed.
    REQUIRE( QFileInfo( aPath ).lastModified() == aMtimeBefore );
}
#endif // SICNU_CLI_BINARY

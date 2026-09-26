// test_execution_authority_11.cpp — WP-A: single execution authority.
//
// Two layers of proof that the execution spine stays converged:
//
//  1. SOURCE-SCAN ARCHITECTURE NET. Scheduler-forming constructs (raw
//     std::thread/jthread pools, QThreadPool, QtConcurrent fan-outs,
//     std::async) are frozen to the audited allowlist captured in
//     .planning/execution-runtime-convergence-11/BASELINE.md (36 files at
//     origin/master@a5b11b7f). A NEW scheduler appearing anywhere else in
//     first-party code fails this test with the offending path — "no second
//     scheduler" is enforced by a gate, not by hope. Vendored QGIS trees
//     (src/core, src/gui) and stubs are out of scope by design; adding a file
//     to the allowlist requires an architecture note in
//     docs/execution/CURRENT_ARCHITECTURE.md.
//
//  2. BEHAVIOR. A submitted job body runs ON a JobEngine worker thread under
//     the engine's job id (the sanctioned 1:1 task↔job authority mapping),
//     and cancellation reaches the body through the engine's cancel hook —
//     the single cancel path recorded in CURRENT_ARCHITECTURE.md. The chunk
//     pipeline's consumer abort is fail-closed (throws, never a silent
//     success return).
#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "runtime/chunk/chunk_pipeline.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

namespace fs = std::filesystem;
using sicnu::jobs::JobEngine;
using sicnu::jobs::JobRequest;
using sicnu::jobs::JobState;

// Frozen allowlist (see file header). Paths are forward-slash-relative to the
// source root and stay sorted. An unlisted hit is actionable: move the
// construct into the owning scheduler, or extend this list AND the
// architecture note when a genuinely new owner is justified.
const std::set<std::string> &schedulerAllowlist()
{
    static const std::set<std::string> allow = {
        "src/agent/agent_copilot_dock_widget.cpp",
        "src/app/dialogs/async_gdal_runner.cpp",
        "src/app/dialogs/stac_browser_dialog.cpp",
        "src/app/map_tools/rs_roi_spectrum_tool.cpp",
        "src/app/preview/asset_preview_service.cpp",
        "src/app/preview/asset_preview_service.h",
        "src/app/selecttools/qgsmaptoolselectutils.cpp",
        "src/app/shell/schema_form_builder.cpp",
        "src/app/shell/schema_form_builder.h",
        "src/app/widgets/histogram_widget.cpp",
        "src/app/widgets/histogram_widget.h",
        "src/app/widgets/roi_statistics_widget.cpp",
        "src/app/widgets/roi_statistics_widget.h",
        "src/app/widgets/rs_scan_pool.h",
        "src/cli/cli_commands.cpp",
        "src/cli/sicnu_worker_main.cpp",
        "src/data/governance/import_center.cpp",
        "src/data/governance/import_center.h",
        "src/data/governance/metadata_pipeline.cpp",
        "src/data/governance/metadata_pipeline.h",
        "src/jobs/job_engine.cpp",
        "src/jobs/job_engine.h",
        "src/plugins/framework/plugin_ui_schema_host.cpp",
        "src/plugins/framework/plugin_ui_schema_host.h",
        "src/plugins/host/plugin_host_worker_main.cpp",
        "src/app/experiment_studio/experiment_studio_dock.cpp",
        "src/app/experiment_studio/experiment_studio_dock.h",
        "src/app/teaching_admin/teaching_admin_dock.cpp",
        "src/app/teaching_admin/teaching_admin_dock.h",
        "src/app/widgets/spectral_profile_widget.cpp",
        "src/geospatial/stac/stac_client.cpp",
        "src/operators/runtime/model_ensemble.cpp",
        "src/processing/framework/task_center.cpp",
        "src/processing/framework/task_center.h",
        "src/sdk/exprs/plugin_registry.h",
        "src/sdk/exprs/plugin_snapshot.cpp",
        "src/sdk/exprs/plugin_snapshot.h",
        "src/teaching_admin/batch_assessment.cpp",
        "src/workflow/ir2_registry_node_executor.cpp",
        "src/processing/algorithms/chunked_processor.cpp",
        "src/processing/framework/provider_algorithm_adapter.cpp",
        "src/runtime/chunk/chunk_graph.h",
        "src/runtime/chunk/chunk_pipeline.cpp",
        "src/runtime/observability/trace.cpp",
        "src/runtime/observability/trace.h",
        "src/sdk/exprs/ipc_channel.cpp",
        "src/sdk/exprs/ipc_channel.h",
        "src/workflow/pipeline_run_coordinator.cpp",
        "src/workflow/pipeline_run_coordinator.h",
        "src/workflow/workflow_runtime.cpp",
    };
    return allow;
}

bool isVendoredTree( const std::string &relDir )
{
    return relDir == "src/core" || relDir == "src/gui" || relDir == "src/stubs" ||
           relDir == "src/native";
}

std::string readFileOrEmpty( const fs::path &p )
{
    std::ifstream in( p, std::ios::binary );
    if ( !in )
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void scanDirectory( const fs::path &dir, const fs::path &srcRoot, const std::regex &pattern,
                    std::set<std::string> &hits )
{
    std::error_code ec;
    for ( fs::directory_iterator it( dir, fs::directory_options::skip_permission_denied, ec ),
          end;
          it != end; it.increment( ec ) )
    {
        if ( ec )
        {
            ec.clear();
            continue;
        }
        std::error_code relEc;
        const std::string rel = fs::relative( it->path(), srcRoot.parent_path(), relEc ).generic_string();
        if ( relEc )
            continue;
        if ( it->is_directory( ec ) )
        {
            if ( isVendoredTree( rel ) )
                continue;
            scanDirectory( it->path(), srcRoot, pattern, hits );
        }
        else
        {
            const std::string ext = it->path().extension().string();
            if ( ext != ".cpp" && ext != ".h" && ext != ".hpp" )
                continue;
            if ( std::regex_search( readFileOrEmpty( it->path() ), pattern ) )
                hits.insert( rel );
        }
    }
}

struct EngineGuard
{
    EngineGuard()
    {
        auto &eng = JobEngine::instance();
        eng.setListener( nullptr );
        eng.clearExecutors();
        eng.setMaxWorkers( 2 );
    }
    ~EngineGuard()
    {
        auto &eng = JobEngine::instance();
        eng.waitUntilIdleForTests( 15000 );
        eng.setListener( nullptr );
        eng.clearExecutors();
        eng.setFallbackExecutor( {} );
    }
    JobEngine &engine() { return JobEngine::instance(); }
};

} // namespace

TEST_CASE( "Scheduler-forming constructs are frozen to the audited allowlist", "[execution][authority]" )
{
    const fs::path srcRoot = fs::path( CMAKE_SOURCE_DIR ) / "src";
    REQUIRE( fs::exists( srcRoot ) );

    static const std::regex pattern(
        R"(std::thread\b|std::jthread\b|QThreadPool|QtConcurrent::|std::async\s*\(|QThread::create|pthread_create|CreateThread\s*\()" );
    std::set<std::string> hits;
    scanDirectory( srcRoot, srcRoot, pattern, hits );

    std::vector<std::string> unlisted;
    for ( const auto &h : hits )
        if ( !schedulerAllowlist().count( h ) )
            unlisted.push_back( h );
    std::vector<std::string> stale;
    for ( const auto &a : schedulerAllowlist() )
        if ( !hits.count( a ) )
            stale.push_back( a );

    for ( const auto &u : unlisted )
        INFO( "NEW scheduler-forming construct outside the allowlist: " << u );
    for ( const auto &s : stale )
        INFO( "Allowlist entry no longer matching (update the frozen list): " << s );
    REQUIRE( unlisted.empty() );
    REQUIRE( stale.empty() );
}

TEST_CASE( "Job bodies run on the one scheduler under the engine's job id",
           "[execution][authority]" )
{
    EngineGuard guard;
    auto &eng = guard.engine();

    std::promise<void> bodyRan;
    std::future<void> bodyRanFuture = bodyRan.get_future();
    bool bodyOnWorkerThread = false;
    std::string bodyJobId;

    JobRequest req;
    req.algorithmId = "authority:probe";
    req.title = "authority-probe";
    req.source = "test";
    const auto id = eng.submit(
        req,
        [&]( const JobRequest &, sicnu::operators::RSOperatorContext & ) -> Json::Value {
            bodyOnWorkerThread = JobEngine::isWorkerThread();
            bodyJobId = JobEngine::currentJobId();
            bodyRan.set_value();
            return Json::Value();
        },
        [] {} );
    REQUIRE_FALSE( id.empty() );
    REQUIRE( bodyRanFuture.wait_for( std::chrono::seconds( 30 ) ) == std::future_status::ready );
    eng.waitUntilIdleForTests( 15000 );

    // Single authority: the body executed on a JobEngine worker and the id
    // the caller holds IS the id the engine attributed to that worker.
    REQUIRE( bodyOnWorkerThread );
    REQUIRE( bodyJobId == id );
    auto snap = eng.snapshot( id );
    REQUIRE( snap.has_value() );
    REQUIRE( snap->state == JobState::Succeeded );
}

TEST_CASE( "Cancellation reaches the body through the engine cancel hook",
           "[execution][authority]" )
{
    EngineGuard guard;
    auto &eng = guard.engine();

    std::atomic<bool> cancelHookFired{ false };
    std::atomic<bool> bodyFinished{ false };

    JobRequest req;
    req.algorithmId = "authority:cancel";
    req.title = "authority-cancel";
    req.source = "test";
    const auto id = eng.submit(
        req,
        [&]( const JobRequest &, sicnu::operators::RSOperatorContext & ) -> Json::Value {
            for ( int i = 0; i < 6000 && !cancelHookFired.load(); ++i )
                std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            bodyFinished = true;
            return Json::Value();
        },
        [&cancelHookFired] { cancelHookFired = true; } );
    REQUIRE_FALSE( id.empty() );

    // Wait until the body is actually Running so cancel targets a Running
    // job. cancel() also returns true for a QUEUED job ("Cancelled while
    // queued"), so cancel success alone proves nothing about the body having
    // started — under loaded-machine timing the queued-cancel won the race
    // and the body never ran. Poll the snapshot for Running first.
    bool cancelled = false;
    for ( int i = 0; i < 600 && !cancelled; ++i )
    {
        const auto current = eng.snapshot( id );
        if ( current.has_value() && current->state == JobState::Running )
        {
            if ( eng.cancel( id ) )
                cancelled = true;
        }
        else
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    REQUIRE( cancelled );
    eng.waitUntilIdleForTests( 15000 );

    REQUIRE( cancelHookFired.load() );
    REQUIRE( bodyFinished.load() );
    auto snap = eng.snapshot( id );
    REQUIRE( snap.has_value() );
    // Cooperative cancel: the body saw the hook and returned; the engine
    // still records Cancelled (never a fake Succeeded).
    REQUIRE( snap->state == JobState::Cancelled );
}

TEST_CASE( "Chunk pipeline consumer abort is fail-closed", "[execution][authority]" )
{
    using namespace sicnu::runtime::chunk;
    TileSpec spec;
    spec.index = 0;
    spec.totalTiles = 4;
    spec.width = spec.height = 2;
    spec.bufferWidth = spec.bufferHeight = 2;
    spec.rasterWidth = spec.rasterHeight = 2;
    spec.bands = 1;

    int produced = 0;
    ChunkPipeline pipeline(
        [&produced, &spec]( TilePayload &out ) {
            if ( produced >= 4 )
                return false;
            produced++;
            out = TilePayload{ spec, std::make_shared<std::vector<float>>( 4, 1.0f ) };
            return true;
        },
        {},
        []( TilePayload && ) { return false; } // abort at the first tile
    );

    // A consumer that stops early is a terminal abort, never a normal return
    // (a silent success here used to mask failed output writes).
    REQUIRE_THROWS_AS( pipeline.run(), ChunkConsumerAborted );
    REQUIRE( pipeline.completedTiles() == 1 );
}

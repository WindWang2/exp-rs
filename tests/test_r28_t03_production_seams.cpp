// tests/test_r28_t03_production_seams.cpp
//
// R28-T03 M1: the PRODUCTION session seam bundle over the real engines.
//
//   makeProductionSeamBundle() → data (grounding probes) / planner
//   (science_context broker → planScientificWork → projectPlanToIr →
//   compileWorkflow) / preflight (preflightIntent) / executor
//   (WorkflowRunCoordinator → TaskCenter → REAL rs: operators) / verifier
//   (harness verifyArtifact) / diagnoser (run error records).
//
// Oracles:
//   * a real 4-band GeoTIFF (band-role + radiometric metadata) drives a REAL
//     NDVI session end to end through the loop — plan valid, preflight ok,
//     execution by the authoritative engine, verification of the produced
//     raster; zero fake seams anywhere in the chain;
//   * a raster without the NIR band BLOCKS preflight and the session refuses
//     to execute (WP04: what is preflighted is what executes);
//   * an unresolvable input stays a typed error in the snapshot — no
//     invented facts;
//   * a missing capability authority fails the bundle closed (typed error,
//     planner seam null → the driver's SEAMS_UNAVAILABLE gate).
#include <catch2/catch_test_macros.hpp>

#include "agent/tools/agent_session_adapter.h"
#include "agent_loop/scientific_agent_session.h"
#include "agent/harness/grounding_tools.h"

#include "operators/rs/rs_operators_init.h"

#include "jobs/job_engine.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "workflow/workflow_run_coordinator.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cstring>
#include <string>
#include <vector>

using namespace sicnu::agent_loop;

namespace
{

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

/// One shared fixture process state: app, engine workers, real operators,
/// grounding tools, capability authority env, broker authorities.
struct ProductionFixture
{
    QTemporaryDir scratch;
    QString checkpointDir;

    ProductionFixture()
    {
        int argc = 1;
        static char arg0[] = "test_r28_t03_production_seams";
        char *argv[] = { arg0, nullptr };
        if ( !QCoreApplication::instance() )
            new QCoreApplication( argc, argv );

        // The capability authority directory the harness CapabilityKnowledge
        // and the planner's LiveCapabilityProvider both read.
        qputenv( "SICNU_CAPABILITIES_DIR",
                 QString( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );

        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );

        // The REAL rs: operators — the engine dispatches through
        // RSOperatorRegistry (ADR 0062 resolution order).
        sicnu::operators::rs::initBuiltinRsOperators();
        sicnu::agent::harness::registerGroundingTools();

        checkpointDir = scratch.filePath( QStringLiteral( "checkpoints" ) );
        QDir().mkpath( checkpointDir );
        sicnu::workflow::WorkflowRunCoordinator::instance().setCheckpointDirectory(
            checkpointDir );
    }

    ~ProductionFixture()
    {
        sicnu::jobs::JobEngine::instance().shutdownForTests();
        sicnu::workflow::WorkflowRunCoordinator::instance().shutdownForTests();
    }
};

/// Writes a small real GeoTIFF whose metadata honestly declares what it is:
/// per-band SICNU_BAND_ROLE and dataset SICNU_RADIOMETRIC_STATE. Band count
/// controls the roles written (blue/green/red[/nir]).
std::string writeSceneRaster( const QString &path, bool withNir )
{
    GDALAllRegister();
    const int bands = withNir ? 4 : 3;
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    if ( !driver )
        return {};
    GDALDataset *dataset = driver->Create( path.toStdString().c_str(), 16, 16, bands,
                                           GDT_Float32, nullptr );
    if ( !dataset )
        return {};
    OGRSpatialReference srs;
    srs.importFromEPSG( 32633 );
    srs.SetAxisMappingStrategy( OAMS_TRADITIONAL_GIS_ORDER );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    dataset->SetProjection( wkt );
    CPLFree( wkt );
    double geotransform[6] = { 400000.0, 10.0, 0.0, 5000000.0, 0.0, -10.0 };
    dataset->SetGeoTransform( geotransform );
    dataset->SetMetadataItem( "SICNU_RADIOMETRIC_STATE", "surface_reflectance", nullptr );

    const char *roles[] = { "blue", "green", "red", "nir" };
    for ( int index = 1; index <= bands; ++index )
    {
        GDALRasterBand *band = dataset->GetRasterBand( index );
        band->SetDescription( roles[ index - 1 ] );
        band->SetMetadataItem( "SICNU_BAND_ROLE", roles[ index - 1 ], nullptr );
        // red=0.3, nir=0.5 → NDVI = (0.5-0.3)/(0.5+0.3) = 0.25
        const float value = index == 4 ? 0.5f : ( index == 3 ? 0.3f : 0.2f );
        const std::vector< float > block( 16 * 16, value );
        band->RasterIO( GF_Write, 0, 0, 16, 16, const_cast< float * >( block.data() ), 16, 16,
                        GDT_Float32, 0, 0 );
        band->FlushCache();
    }
    dataset->FlushCache();
    GDALClose( dataset );
    return path.toStdString();
}

ScientificAgentSession::Dependencies productionDeps( sicnu::agent::ProductionSeamBundle &bundle )
{
    return { bundle.data.get(), bundle.planner.get(), bundle.preflight.get(),
             bundle.executor.get(), bundle.verifier.get(), bundle.diagnoser.get() };
}

} // namespace

TEST_CASE( "production seams run a real NDVI chain end to end",
           "[r28_t03][production_seams][agent_session]" )
{
    ProductionFixture fx;
    const std::string scene = writeSceneRaster( fx.scratch.filePath( QStringLiteral( "scene.tif" ) ),
                                                /*withNir=*/true );
    REQUIRE_FALSE( scene.empty() );

    sicnu::agent::ProductionSeamBundle bundle = sicnu::agent::makeProductionSeamBundle();
    REQUIRE( bundle.error.empty() );
    REQUIRE( bundle.data != nullptr );
    REQUIRE( bundle.planner != nullptr );
    REQUIRE( bundle.preflight != nullptr );
    REQUIRE( bundle.executor != nullptr );
    REQUIRE( bundle.verifier != nullptr );
    REQUIRE( bundle.diagnoser != nullptr );

    // The full evidence-first loop over the real engines (no fake seams).
    SessionRunRequest request;
    request.goal = "compute NDVI for the scene";
    request.intent = "ndvi";
    Json::Value refs( Json::objectValue );
    refs[ "primary" ] = scene;
    request.refs = refs;

    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::ExecuteWithVerify;
    policy.executorTimeoutMs = 60000; // real GDAL chain: minutes-free but not instant
    auto deps = productionDeps( bundle );
    ScientificAgentSession session( policy, deps, {}, "sess-r28-t03-ndvi" );
    const SessionResult result = session.run( request );

    INFO( "terminal=" << result.terminalState << " stop=" << result.stopReason
                      << " verify=" << result.summary.verificationVerdict );
    REQUIRE( result.ok );
    REQUIRE( result.summary.verificationVerdict == "PASS" );
    REQUIRE_FALSE( result.summary.artifacts.empty() );
}

TEST_CASE( "a scene without NIR blocks preflight and the session refuses to execute",
           "[r28_t03][production_seams][preflight]" )
{
    ProductionFixture fx;
    const std::string scene = writeSceneRaster( fx.scratch.filePath( QStringLiteral( "no_nir.tif" ) ),
                                                /*withNir=*/false );
    REQUIRE_FALSE( scene.empty() );

    sicnu::agent::ProductionSeamBundle bundle = sicnu::agent::makeProductionSeamBundle();
    REQUIRE( bundle.error.empty() );
    auto deps = productionDeps( bundle );

    SessionRunRequest request;
    request.goal = "compute NDVI for the scene";
    request.intent = "ndvi";
    Json::Value refs( Json::objectValue );
    refs[ "primary" ] = scene;
    request.refs = refs;

    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::ExecuteWithVerify;
    ScientificAgentSession session( policy, deps, {}, "sess-r28-t03-blocked" );
    const SessionResult result = session.run( request );

    INFO( "terminal=" << result.terminalState << " stop=" << result.stopReason );
    // The real rule pack blocks the missing-NIR scene; the session must not
    // deliver success nor execute the plan.
    REQUIRE_FALSE( result.ok );
    REQUIRE_FALSE( result.summary.verificationVerdict == "PASS" );
}

TEST_CASE( "the data state provider reports unresolvable refs as typed errors",
           "[r28_t03][production_seams][data_state]" )
{
    ProductionFixture fx;
    sicnu::agent::ProductionSeamBundle bundle = sicnu::agent::makeProductionSeamBundle();
    REQUIRE( bundle.data != nullptr );

    Json::Value refs( Json::objectValue );
    refs[ "primary" ] = fx.scratch.filePath( QStringLiteral( "does_not_exist.tif" ) ).toStdString();
    refs[ "bad_shape" ] = 42; // wrong-typed ref: typed refusal, never a crash

    const DataStateSnapshot snapshot = bundle.data->snapshot( "understand the scene", refs );
    REQUIRE( snapshot.assets.size() == 2 );
    bool sawMissing = false;
    bool sawInvalid = false;
    for ( const auto &asset : snapshot.assets )
    {
        if ( asset.slot == "primary" )
        {
            sawMissing = !asset.resolved && !asset.resolutionError.empty();
            REQUIRE( asset.kind == "unknown" );
        }
        if ( asset.slot == "bad_shape" )
            sawInvalid = asset.resolutionError == "INVALID_REF";
    }
    REQUIRE( sawMissing );
    REQUIRE( sawInvalid );
    REQUIRE( snapshot.summary.get( "resolved", 0 ).asInt() == 0 );
}

TEST_CASE( "a missing capability authority fails the bundle closed",
           "[r28_t03][production_seams][fail_closed]" )
{
    // Restore the fixture's authority afterwards — the shared fixture state
    // (registry etc.) must stay intact for the other cases.
    const QByteArray original = qgetenv( "SICNU_CAPABILITIES_DIR" );
    qputenv( "SICNU_CAPABILITIES_DIR", "/nonexistent-r28-t03-capabilities" );

    std::unique_ptr< sicnu::agent::HarnessPlanner > planner;
    std::string error;
    REQUIRE_FALSE( sicnu::agent::HarnessPlanner::create( planner, error ) );
    REQUIRE( planner == nullptr );
    REQUIRE_FALSE( error.empty() );

    if ( !original.isEmpty() )
        qputenv( "SICNU_CAPABILITIES_DIR", original );
    else
        qunsetenv( "SICNU_CAPABILITIES_DIR" );
}

TEST_CASE( "identity pins block swapped inputs at preflight and executor",
           "[r28_t03][production_seams][identity]" )
{
    ProductionFixture fx;
    const std::string scene = writeSceneRaster( fx.scratch.filePath( QStringLiteral( "scene.tif" ) ),
                                                /*withNir=*/true );
    REQUIRE_FALSE( scene.empty() );
    const std::string other = writeSceneRaster( fx.scratch.filePath( QStringLiteral( "other.tif" ) ),
                                                /*withNir=*/true );
    REQUIRE_FALSE( other.empty() );

    sicnu::agent::ProductionSeamBundle bundle = sicnu::agent::makeProductionSeamBundle();
    REQUIRE( bundle.error.empty() );

    // Plan once over the scene: the draft carries plan-time bindings + pins.
    Json::Value refs( Json::objectValue );
    refs[ "primary" ] = scene;
    const DataStateSnapshot snapshot = bundle.data->snapshot( "understand", refs );

    sicnu::agent_loop::PlanRequest request;
    request.goal = "compute NDVI for the scene";
    request.intent = "ndvi";
    request.snapshot = snapshot;
    const sicnu::agent_loop::PlanDraft draft = bundle.planner->plan( request );
    REQUIRE( draft.valid );
    REQUIRE( draft.bindings.isObject() );
    REQUIRE( draft.bindings.isMember( "inputs" ) );

    SECTION( "an honest re-check of the same input passes the identity gate" )
    {
        const sicnu::agent_loop::PreflightReport report = bundle.preflight->check( draft, snapshot );
        CHECK( report.verdict != "blocked" );
    }

    SECTION( "a swapped pin is blocked at preflight with a typed identity error" )
    {
        sicnu::agent_loop::PlanDraft tampered = draft;
        REQUIRE( tampered.bindings[ "pins" ][ "datasets" ].isObject() );
        tampered.bindings[ "pins" ][ "datasets" ][ "primary" ][ "path" ] = other;
        const sicnu::agent_loop::PreflightReport report =
            bundle.preflight->check( tampered, snapshot );
        CHECK( report.verdict == "blocked" );
        REQUIRE_FALSE( report.issues.empty() );
        CHECK( report.issues.front().code == "IDENTITY_MISMATCH" );
    }

    SECTION( "the executor refuses the same tampered plan at the trust transition" )
    {
        sicnu::agent_loop::PlanDraft tampered = draft;
        tampered.bindings[ "pins" ][ "datasets" ][ "primary" ][ "path" ] = other;
        const sicnu::agent_loop::ExecutionStart start = bundle.executor->begin( tampered );
        CHECK_FALSE( start.started );
        CHECK( start.error == "IDENTITY_MISMATCH" );
    }

    SECTION( "a deleted input blocks the preflight identity gate" )
    {
        REQUIRE( QFile::remove( QString::fromStdString( scene ) ) );
        const sicnu::agent_loop::PreflightReport report = bundle.preflight->check( draft, snapshot );
        CHECK( report.verdict == "blocked" );
    }
}

// tests/test_harness_evals.cpp
//
// Harness 4.0 evaluation suite (mission Phase 18) + anti-hallucination
// contract (Phase 19) + token budget checks (Phase 20).
//
// Deterministic, no external model: the "driver" is the harness tool surface
// itself. Each scenario replays the canonical agent loop — ground the data,
// preflight scientifically, instantiate/compile the plan, execute through the
// authoritative workflow engine, verify outputs — and grades the observable
// contract (tool selection, parameter correctness, entity resolution,
// preflight verdicts, execution status, verification verdicts).
//
// Scenario coverage (4.0 + 7.0):
//   1. NDVI (optical vegetation)      — full execution (Tier B)
//   2. Optical bi-temporal change     — full execution (Tier B)
//   3. Sentinel-1 SAR change          — preflight + plan (Tier A)
//   4. Land-cover classification      — preflight + plan (Tier A)
//   5. One-year phenology             — preflight + plan (Tier A)
//   6. Paper figure (vegetation + map output) — plan contract (Tier A)
//   7. Optical flood NDWI             — full execution (Tier B, 7.0)
//   8. SAR flood gates + multimodal   — preflight + compile + preset (Tier A, 7.0)
//   9. DEM terrain                    — full execution (Tier B, 7.0)
//  10. Temporal trend facts           — preflight pack (Tier A, 7.0)
//  11. Inference model contract       — preflight pack (Tier A, 7.0)
//  12. Bounded plan repair            — tool contract (Tier A, 7.0)
//  13. Intent ambiguity + decisions   — graph + ledger (Tier A, 7.0)
//  14. Understanding cache + bindings — context continuity (Tier B, 7.0)

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include "agent/contracts/spatial_contracts.h"
#include "agent/harness/agent_plan.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/harness_verification.h"
#include "agent/harness/plan_tools.h"
#include "agent/harness/recipe_catalog.h"
#include "agent/harness/scientific_preflight.h"
#include "agent/harness/tool_manifest.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include <cpl_vsi.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <ogr_spatialref.h>

#include <json/json.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

using namespace sicnu::agent::harness;
using namespace sicnu::agent::spatial_tools;

namespace {

struct GdalInit
{
    GdalInit()
    {
        GDALAllRegister();
        OGRRegisterAll();
    }
};
static GdalInit s_gdalInit;

struct Registries
{
    Registries() { sicnu::processing::AtomicAlgorithmRegistry::instance().initialize(); }
};
static Registries s_registries;

/// 4-band optical product fixture (Blue/Red/NIR/SWIR order, NIR=4 and RED=3
/// matching the spectral-index operator's positional defaults for real
/// products; roles are declared on every band).
std::string writeOpticalRaster( const QString &path, int width = 16, int height = 16,
                                float seed = 1.0f )
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), width, height, 4,
                                      GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
    ds->SetGeoTransform( gt );
    OGRSpatialReference srs;
    srs.importFromEPSG( 32650 );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    ds->SetProjection( wkt );
    CPLFree( wkt );
    ds->GetRasterBand( 1 )->SetMetadataItem( "SICNU_BAND_ROLE", "BLUE", nullptr );
    ds->GetRasterBand( 2 )->SetMetadataItem( "SICNU_BAND_ROLE", "GREEN", nullptr );
    ds->GetRasterBand( 3 )->SetMetadataItem( "SICNU_BAND_ROLE", "RED", nullptr );
    ds->GetRasterBand( 4 )->SetMetadataItem( "SICNU_BAND_ROLE", "NIR", nullptr );
    std::vector<float> row( static_cast<size_t>( width ) );
    for ( int y = 0; y < height; ++y )
    {
        for ( int x = 0; x < width; ++x )
            row[static_cast<size_t>( x )] =
              0.1f + seed * 0.01f * static_cast<float>( ( y * width + x ) % 32 );
        ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, width, 1, row.data(), width, 1,
                                          GDT_Float32, 0, 0 );
        ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, y, width, 1, row.data(), width, 1,
                                          GDT_Float32, 0, 0 );
        for ( int x = 0; x < width; ++x )
            row[static_cast<size_t>( x )] *= 0.8f;
        ds->GetRasterBand( 3 )->RasterIO( GF_Write, 0, y, width, 1, row.data(), width, 1,
                                          GDT_Float32, 0, 0 );
        ds->GetRasterBand( 4 )->RasterIO( GF_Write, 0, y, width, 1, row.data(), width, 1,
                                          GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return path.toStdString();
}

std::string writeSarRaster( const QString &path, const char *polarization, int epsg = 32650 )
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 16, 16, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
    ds->SetGeoTransform( gt );
    OGRSpatialReference srs;
    srs.importFromEPSG( epsg );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    ds->SetProjection( wkt );
    CPLFree( wkt );
    ds->GetRasterBand( 1 )->SetMetadataItem( "SICNU_BAND_ROLE", polarization, nullptr );
    std::vector<float> row( 16, 0.5f );
    for ( int y = 0; y < 16; ++y )
        ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, 16, 1, row.data(), 16, 1,
                                          GDT_Float32, 0, 0 );
    GDALClose( ds );
    return path.toStdString();
}

SpatialToolResult callTool( const std::string &name, const Json::Value &input )
{
    auto tool = SpatialToolRegistry::instance().find( name );
    REQUIRE( tool.has_value() );
    return ( *tool )->execute( input );
}

Json::Value parseJson( const std::string &text )
{
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( text.c_str(), text.c_str() + text.size(), &parsed, &errors ) )
        FAIL( "JSON parse failed: " << errors );
    return parsed;
}

bool waitForTerminal( const std::string &runId, int timeoutMs = 60000 )
{
    // NOTE: poll through the harness:run_status TOOL, not the coordinator
    // singleton — sicnu_task_center is a static library linked into both the
    // test exe and sicnu_agent.dll, so each holds its own singletons. The
    // tool surface (which is what the eval drives) is self-consistent.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds( timeoutMs );
    std::string lastState = "?";
    while ( std::chrono::steady_clock::now() < deadline )
    {
        Json::Value statusInput;
        statusInput["run_id"] = runId;
        const SpatialToolResult status = callTool( "harness:run_status", statusInput );
        if ( status.success )
        {
            lastState = status.output["state"].asString();
            if ( lastState == "Completed" || lastState == "Failed" ||
                 lastState == "Canceled" || lastState == "Interrupted" )
                return true;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }
    FAIL( "run did not go terminal: state=" << lastState );
    return false;
}

/// Executes a plan through the harness tools and returns the final
/// run_status document (the deterministic execution driver).
Json::Value executePlanToTerminal( const Json::Value &plan )
{
    Json::Value execInput;
    execInput["plan"] = plan;
    const SpatialToolResult submitted = callTool( "harness:execute_plan", execInput );
    if ( !submitted.success )
    {
        Json::Value failure( Json::objectValue );
        failure["status"] = "submission_failed";
        failure["code"] = submitted.errorCode;
        failure["error"] = submitted.error;
        FAIL( "submission failed: " << submitted.error );
        return failure;
    }

    if ( !submitted.output.get( "executed", false ).asBool() )
        return submitted.output; // preflight refusal document
    const std::string runId = submitted.output["run_id"].asString();
    REQUIRE( waitForTerminal( runId ) );
    Json::Value statusInput;
    statusInput["run_id"] = runId;
    statusInput["plan"] = plan;
    statusInput["auto_resume_transient"] = false;
    const SpatialToolResult status = callTool( "harness:run_status", statusInput );
    REQUIRE( status.success );
    if ( status.output.get( "status", "" ).asString() == "failed" )
        UNSCOPED_INFO( "run failed; verification doc: "
              << Json::writeString( Json::StreamWriterBuilder(), status.output["verification"] ) );
    return status.output;
}

} // namespace

// ---------------------------------------------------------------------------
// Tier A: contract scenarios (grounding, preflight, plan compile, estimates)
// ---------------------------------------------------------------------------

TEST_CASE( "Eval: optical vegetation NDVI full pipeline", "[harness][eval][ndvi]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    RecipeCatalog::instance().setDirectory( ( std::filesystem::path( CMAKE_SOURCE_DIR ) /
                                              "data/agent/recipes" ).string() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string raster = writeOpticalRaster( tmp.filePath( "scene.tif" ) );

    // 1. Ground: typed understanding, no guessing.
    Json::Value understandInput;
    understandInput["asset"] = raster;
    const SpatialToolResult understood = callTool( "spatial:understand", understandInput );
    REQUIRE( understood.success );
    CHECK( understood.output["dataset_understanding"]["modality"].asString() == "optical" );
    CHECK( understood.output["dataset_understanding"]["band_count"].asInt() == 4 );

    // 2. Instantiate the sanctioned recipe.
    Json::Value bindings;
    bindings["slots"]["primary"] = raster;
    bindings["output_dir"] = tmp.path().toStdString();
    Json::Value instInput;
    instInput["recipe_id"] = "harness.optical_vegetation";
    instInput["bindings"] = bindings;
    const SpatialToolResult instantiated = callTool( "harness:instantiate_recipe", instInput );
    REQUIRE( instantiated.success );
    const Json::Value plan = instantiated.output["plan"];
    CHECK( plan["intent"].asString() == "ndvi" );
    REQUIRE( plan["steps"].size() == 1 );
    CHECK( plan["steps"][0]["operator_id"].asString() == "rs:spectral_index" );
    CHECK( plan["steps"][0]["params"]["index"].asString() == "NDVI" );

    // 3. Preflight must pass on a proper optical product.
    Json::Value preflightInput;
    preflightInput["intent"] = "ndvi";
    Json::Value refs( Json::arrayValue );
    Json::Value ref( Json::objectValue );
    ref["name"] = "primary";
    ref["ref"] = raster;
    refs.append( ref );
    preflightInput["inputs"] = refs;
    const SpatialToolResult preflight = callTool( "harness:preflight", preflightInput );
    REQUIRE( preflight.success );
    CHECK( preflight.output["preflight"]["verdict"].asString() == "ok" );

    // 4. Compile + resource estimate contract.
    AgentPlan parsed;
    HarnessError error;
    REQUIRE( readAgentPlan( plan, parsed, error ) );
    const Json::Value estimates = estimatePlanResources( parsed );
    CHECK( estimates["total_ram_mb"].asInt64() >= 0 );
    const std::string workflowJson = compilePlanToWorkflowJson( parsed, error );
    REQUIRE( !workflowJson.empty() );
    const Json::Value workflow = parseJson( workflowJson );
    CHECK( workflow["steps"][0]["operatorId"].asString() == "rs:spectral_index" );
    CHECK( !workflow["steps"][0]["params"]["input"].asString().empty() );

    // 5. Execute through the authoritative engine and verify.
    {
        sicnu::workflow::WorkflowDefinition def;
        std::string defErr;
        REQUIRE( sicnu::workflow::workflowDefinitionFromJson( parseJson( workflowJson ), def,
                                                              defErr ) );
    }
    const Json::Value status = executePlanToTerminal( plan );
    CHECK( status["state"].asString() == "Completed" );
    CHECK( status["status"].asString() == "completed" );
    CHECK( status["verification"]["verdict"].asString() == "PASS" );
}

TEST_CASE( "Eval: optical bi-temporal change full pipeline", "[harness][eval][change]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    RecipeCatalog::instance().setDirectory( ( std::filesystem::path( CMAKE_SOURCE_DIR ) /
                                              "data/agent/recipes" ).string() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string before = writeOpticalRaster( tmp.filePath( "t1.tif" ), 16, 16, 1.0f );
    const std::string after = writeOpticalRaster( tmp.filePath( "t2.tif" ), 16, 16, 3.0f );

    Json::Value bindings;
    bindings["slots"]["before"] = before;
    bindings["slots"]["after"] = after;
    bindings["output_dir"] = tmp.path().toStdString();
    Json::Value instInput;
    instInput["recipe_id"] = "harness.optical_change";
    instInput["bindings"] = bindings;
    const SpatialToolResult instantiated = callTool( "harness:instantiate_recipe", instInput );
    REQUIRE( instantiated.success );
    const Json::Value plan = instantiated.output["plan"];
    // align gate closed (no align param) -> alignment steps dropped, difference
    // reads the bound paths directly.
    CHECK( plan["steps"].size() == 2 );
    CHECK( plan["steps"][0]["operator_id"].asString() == "rs:change_difference" );
    CHECK( plan["steps"][0]["params"]["before"].asString() == before );

    const Json::Value status = executePlanToTerminal( plan );
    CHECK( status["state"].asString() == "Completed" );
    CHECK( status["verification"]["verdict"].asString() == "PASS" );
}

TEST_CASE( "Eval: SAR change preflight blocks modality and polarization mismatches",
           "[harness][eval][sar]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    RecipeCatalog::instance().setDirectory( ( std::filesystem::path( CMAKE_SOURCE_DIR ) /
                                              "data/agent/recipes" ).string() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string hh = writeSarRaster( tmp.filePath( "s1_hh.tif" ), "HH" );
    const std::string vv = writeSarRaster( tmp.filePath( "s1_vv.tif" ), "VV" );
    const std::string optical = writeOpticalRaster( tmp.filePath( "s2.tif" ) );

    auto preflightSar = [ & ]( const std::string & a, const std::string & b ) {
        Json::Value input;
        input["intent"] = "sar_change";
        Json::Value refs( Json::arrayValue );
        Json::Value ra( Json::objectValue );
        ra["name"] = "reference";
        ra["ref"] = a;
        refs.append( ra );
        Json::Value rb( Json::objectValue );
        rb["name"] = "observation";
        rb["ref"] = b;
        refs.append( rb );
        input["inputs"] = refs;
        const SpatialToolResult result = callTool( "harness:preflight", input );
        REQUIRE( result.success );
        return result.output["preflight"];
    };

    // Matching polarizations: no polarization blocker (may carry advisories).
    const Json::Value matching = preflightSar( hh, hh );
    bool polarizationBlocked = false;
    for ( const auto &issue : matching["issues"] )
        if ( issue["code"].asString() == "POLARIZATION_MISMATCH" &&
             issue["severity"].asString() == "error" )
            polarizationBlocked = true;
    CHECK( !polarizationBlocked );

    // Cross polarization: blocked.
    CHECK( preflightSar( hh, vv )["verdict"].asString() == "blocked" );

    // SAR vs optical: modality mismatch blocks.
    CHECK( preflightSar( hh, optical )["verdict"].asString() == "blocked" );

    // A clean same-pol pair instantiates and compiles (no execution: SAR
    // calibration here would run the real SAR operators — covered by their
    // own suites; the eval grades the harness contract).
    Json::Value bindings;
    bindings["slots"]["reference"] = hh;
    bindings["slots"]["observation"] = hh;
    bindings["output_dir"] = tmp.path().toStdString();
    Json::Value instInput;
    instInput["recipe_id"] = "harness.sar_change";
    instInput["bindings"] = bindings;
    const SpatialToolResult instantiated = callTool( "harness:instantiate_recipe", instInput );
    REQUIRE( instantiated.success );
    AgentPlan parsed;
    HarnessError error;
    REQUIRE( readAgentPlan( instantiated.output["plan"], parsed, error ) );
    // DEM unbound -> terrain flatten steps dropped, speckle reads calibrated.
    bool sawFlatten = false;
    for ( const auto &step : parsed.steps )
        if ( step["id"].asString() == "flatten_reference" )
            sawFlatten = true;
    CHECK( !sawFlatten );
    CHECK( compilePlanToWorkflowJson( parsed, error ).size() > 0 );
}

TEST_CASE( "Eval: land cover classification plan contract", "[harness][eval][classify]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    RecipeCatalog::instance().setDirectory( ( std::filesystem::path( CMAKE_SOURCE_DIR ) /
                                              "data/agent/recipes" ).string() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string raster = writeOpticalRaster( tmp.filePath( "stack.tif" ) );

    // classify preflight without training input must be blocked.
    Json::Value refs( Json::arrayValue );
    Json::Value ref( Json::objectValue );
    ref["name"] = "primary";
    ref["ref"] = raster;
    refs.append( ref );
    Json::Value preflightInput;
    preflightInput["intent"] = "classify";
    preflightInput["inputs"] = refs;
    const SpatialToolResult preflight = callTool( "harness:preflight", preflightInput );
    REQUIRE( preflight.success );
    CHECK( preflight.output["preflight"]["verdict"].asString() == "blocked" );

    // Land-cover recipe without a training binding fails typed, not silently.
    Json::Value bindings;
    bindings["slots"]["primary"] = raster;
    bindings["output_dir"] = tmp.path().toStdString();
    Json::Value instInput;
    instInput["recipe_id"] = "harness.land_cover";
    instInput["bindings"] = bindings;
    const SpatialToolResult instantiated = callTool( "harness:instantiate_recipe", instInput );
    CHECK( !instantiated.success );
    CHECK( instantiated.errorCode == "INVALID_PARAMETER" );
}

TEST_CASE( "Eval: phenology plan contract", "[harness][eval][phenology]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    RecipeCatalog::instance().setDirectory( ( std::filesystem::path( CMAKE_SOURCE_DIR ) /
                                              "data/agent/recipes" ).string() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string raster = writeOpticalRaster( tmp.filePath( "veg.tif" ) );

    Json::Value bindings;
    bindings["slots"]["collection"] = raster;
    bindings["output_dir"] = tmp.path().toStdString();
    Json::Value instInput;
    instInput["recipe_id"] = "harness.phenology";
    instInput["bindings"] = bindings;
    const SpatialToolResult instantiated = callTool( "harness:instantiate_recipe", instInput );
    REQUIRE( instantiated.success );
    const Json::Value plan = instantiated.output["plan"];
    CHECK( plan["steps"].size() == 4 );
    CHECK( plan["steps"][0]["operator_id"].asString() == "rs:temporal_index_series" );
    AgentPlan parsed;
    HarnessError error;
    REQUIRE( readAgentPlan( plan, parsed, error ) );
    CHECK( compilePlanToWorkflowJson( parsed, error ).size() > 0 );
}

TEST_CASE( "Eval: paper figure plan declares map output", "[harness][eval][figure]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    RecipeCatalog::instance().setDirectory( ( std::filesystem::path( CMAKE_SOURCE_DIR ) /
                                              "data/agent/recipes" ).string() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string raster = writeOpticalRaster( tmp.filePath( "scene.tif" ) );
    Json::Value bindings;
    bindings["slots"]["primary"] = raster;
    bindings["params"]["classField"] = "class_id";
    bindings["output_dir"] = tmp.path().toStdString();
    bindings["layout_name"] = "paper-figure-1";
    Json::Value instInput;
    instInput["recipe_id"] = "harness.optical_vegetation";
    instInput["bindings"] = bindings;
    const SpatialToolResult instantiated = callTool( "harness:instantiate_recipe", instInput );
    REQUIRE( instantiated.success );
    const Json::Value plan = instantiated.output["plan"];
    CHECK( plan.isMember( "map_output" ) );
    CHECK( plan["map_output"]["from_step"].asString() == "ndvi" );
    CHECK( plan["verification"]["enabled"].asBool() );
}

// ---------------------------------------------------------------------------
// Anti-hallucination contract (Phase 19): unknown/ambiguous -> typed failure.
// ---------------------------------------------------------------------------

TEST_CASE( "Eval: anti-hallucination unknown and ambiguous references fail typed",
           "[harness][eval][antihallucination]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();

    SECTION( "unknown dataset" )
    {
        Json::Value input;
        input["asset"] = "asset-987654";
        const SpatialToolResult result = callTool( "spatial:understand", input );
        CHECK( !result.success );
        CHECK( result.errorCode == "DATASET_NOT_FOUND" );
    }
    SECTION( "unknown run id" )
    {
        Json::Value input;
        input["run_id"] = "run-does-not-exist";
        const SpatialToolResult result = callTool( "harness:run_status", input );
        CHECK( !result.success );
        CHECK( result.errorCode == "WORKFLOW_NOT_FOUND" );
    }
    SECTION( "unknown operator in a plan" )
    {
        AgentPlan plan;
        plan.planId = "plan-bad";
        Json::Value step( Json::objectValue );
        step["id"] = "s1";
        step["operator_id"] = "rs:teleport";
        plan.steps.append( step );
        const std::vector<AgentPlanIssue> issues = validateAgentPlan( plan );
        REQUIRE( !issues.empty() );
        bool unknownOperator = false;
        for ( const auto &issue : issues )
            if ( issue.error.summary.find( "rs:teleport" ) != std::string::npos )
                unknownOperator = true;
        CHECK( unknownOperator );
        HarnessError error;
        CHECK( compilePlanToWorkflowJson( plan, error ).empty() );
    }
}

// ---------------------------------------------------------------------------
// FAIL never surfaces as success (Phase 9 acceptance): a verified output that
// disappears forces the run status to failed.
// ---------------------------------------------------------------------------

TEST_CASE( "Eval: FAIL verification is never reported as success",
           "[harness][eval][verification]" )
{
    const std::string missing = "/nonexistent/harness_eval_output.tif";
    VerificationExpectations expectations;
    const ArtifactVerification artifact = verifyArtifact( missing, expectations );
    CHECK( artifact.verdict == Verdict::Fail );
    CHECK( aggregateVerdict( { artifact } ) == Verdict::Fail );
    CHECK( std::string( verdictToStringWire( Verdict::Fail ) ) == "FAIL" );
}

// ---------------------------------------------------------------------------
// Token budget contract (Phase 20): staged discovery stays bounded.
// ---------------------------------------------------------------------------

TEST_CASE( "Eval: token budgets manifests error catalog and context",
           "[harness][eval][tokens]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();

    auto budgetBytes = []( const Json::Value &doc ) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString( builder, doc ).size();
    };

    // One manifest page (50 tools) must stay under 64 KiB.
    Json::Value pageInput;
    pageInput["limit"] = 50;
    const SpatialToolResult manifests = callTool( "harness:tool_manifest", pageInput );
    REQUIRE( manifests.success );
    CHECK( budgetBytes( manifests.output ) < 64 * 1024 );

    // The error taxonomy fits comfortably under 8 KiB.
    const SpatialToolResult codes = callTool( "harness:error_codes", Json::Value() );
    REQUIRE( codes.success );
    CHECK( budgetBytes( codes.output ) < 8 * 1024 );

    // The typed context stays under 256 KiB even with a busy workspace.
    const SpatialToolResult context = callTool( "harness:context", Json::Value() );
    REQUIRE( context.success );
    const size_t contextBytes = budgetBytes( context.output );
    CHECK( contextBytes < 256 * 1024 );

    // Optional benchmark dump (Phase 20): SICNU_BENCH_OUT=<file> records the
    // measured sizes alongside the caps.
    if ( const char *benchOut = std::getenv( "SICNU_BENCH_OUT" ) )
    {
        Json::Value bench( Json::objectValue );
        bench["schema_version"] = "1.0";
        bench["kind"] = "harness_token_budgets";
        Json::Value measured( Json::objectValue );
        measured["harness_error_codes_bytes"] = static_cast<Json::UInt64>( budgetBytes( codes.output ) );
        measured["harness_tool_manifest_page_50_bytes"] =
          static_cast<Json::UInt64>( budgetBytes( manifests.output ) );
        measured["harness_context_bytes"] = static_cast<Json::UInt64>( contextBytes );
        bench["measured"] = measured;
        Json::Value caps( Json::objectValue );
        caps["error_catalog_max_bytes"] = 8 * 1024;
        caps["manifest_page_max_bytes"] = 64 * 1024;
        caps["context_max_bytes"] = 256 * 1024;
        caps["registry_tool_output_cap_bytes"] = static_cast<Json::UInt64>( sicnu::agent::contracts::kMaxToolOutputBytes );
        bench["caps"] = caps;
        Json::StreamWriterBuilder benchWriter;
        benchWriter["indentation"] = "  ";
        QFile out( QString::fromUtf8( benchOut ) );
        if ( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        {
            out.write( QByteArray::fromStdString( Json::writeString( benchWriter, bench ) ) );
            out.close();
        }
    }
}

// ---------------------------------------------------------------------------
// Harness 7.0 scenarios (mission Areas B/C/D/F/G/H)
// ---------------------------------------------------------------------------

namespace {

/// DEM fixture: 1-band elevation raster in a projected CRS.
std::string writeDemRaster( const QString &path )
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 16, 16, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
    ds->SetGeoTransform( gt );
    OGRSpatialReference srs;
    srs.importFromEPSG( 32650 );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    ds->SetProjection( wkt );
    CPLFree( wkt );
    ds->GetRasterBand( 1 )->SetMetadataItem( "SICNU_BAND_ROLE", "DEM", nullptr );
    for ( int y = 0; y < 16; ++y )
    {
        std::vector<float> row( 16 );
        for ( int x = 0; x < 16; ++x )
            row[static_cast<size_t>( x )] = 100.0f + 4.0f * static_cast<float>( x + y );
        ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, 16, 1, row.data(), 16, 1,
                                          GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return path.toStdString();
}

Json::Value preflightRefs( const std::string &intent, const Json::Value &refs )
{
    Json::Value input;
    input["intent"] = intent;
    input["inputs"] = refs;
    const SpatialToolResult result = callTool( "harness:preflight", input );
    REQUIRE( result.success );
    return result.output["preflight"];
}

bool hasBlocker( const Json::Value &preflight, const std::string &code )
{
    for ( const auto &issue : preflight["issues"] )
        if ( issue["code"].asString() == code && issue["severity"].asString() == "error" )
            return true;
    return false;
}

} // namespace

TEST_CASE( "Eval: optical flood NDWI full pipeline", "[harness][eval][flood]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    RecipeCatalog::instance().setDirectory( ( std::filesystem::path( CMAKE_SOURCE_DIR ) /
                                              "data/agent/recipes" ).string() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string raster = writeOpticalRaster( tmp.filePath( "flood_scene.tif" ) );
    Json::Value bindings;
    bindings["slots"]["primary"] = raster;
    bindings["output_dir"] = tmp.path().toStdString();
    Json::Value instInput;
    instInput["recipe_id"] = "harness.optical_ndwi";
    instInput["bindings"] = bindings;
    const SpatialToolResult instantiated = callTool( "harness:instantiate_recipe", instInput );
    REQUIRE( instantiated.success );
    AgentPlan parsed;
    HarnessError error;
    REQUIRE( readAgentPlan( instantiated.output["plan"], parsed, error ) );

    const Json::Value status = executePlanToTerminal( instantiated.output["plan"] );
    CHECK( status["status"].asString() == "completed" );
    CHECK( status["verification"]["verdict"].asString() != "FAIL" );
    // The declared water product exists ( floods the recipe output contract ).
    const std::string classified = tmp.path().toStdString() + "/harness.optical_ndwi_classified.tif";
    CHECK( QFile::exists( QString::fromStdString( classified ) ) );
}

TEST_CASE( "Eval: SAR flood preflight multimodal and polarization gates", "[harness][eval][sar-flood]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string vhA = writeSarRaster( tmp.filePath( "s1a_vh.tif" ), "VH" );
    const std::string vhB = writeSarRaster( tmp.filePath( "s1b_vh.tif" ), "VH" );
    const std::string vv = writeSarRaster( tmp.filePath( "s1_vv.tif" ), "VV" );
    const std::string offGrid = writeSarRaster( tmp.filePath( "s1_offgrid.tif" ), "VH", 32633 );
    const std::string optical = writeOpticalRaster( tmp.filePath( "s2.tif" ) );

    // VH/VH pair: no polarization blocker.
    Json::Value refs;
    Json::Value ra, rb;
    ra["name"] = "primary"; ra["ref"] = vhA; refs.append( ra );
    rb["name"] = "secondary"; rb["ref"] = vhB; refs.append( rb );
    const Json::Value ok = preflightRefs( "sar_flood", refs );
    CHECK( !hasBlocker( ok, "POLARIZATION_MISMATCH" ) );

    // VH/VV: polarization blocker.
    refs[1]["ref"] = vv;
    CHECK( hasBlocker( preflightRefs( "sar_flood", refs ), "POLARIZATION_MISMATCH" ) );

    // Mixed optical+SAR flood on different grids: multimodal CRS blocker.
    Json::Value mixed;
    Json::Value mo, ms;
    mo["name"] = "optical"; mo["ref"] = optical; mixed.append( mo );
    ms["name"] = "sar"; ms["ref"] = offGrid; mixed.append( ms );
    CHECK( hasBlocker( preflightRefs( "flood", mixed ), "CRS_MISMATCH" ) );

    // Same-grid mixed inputs: cross-modality registration advisories, but the
    // fused intent is not blocked.
    Json::Value mixedOk;
    Json::Value mo2, ms2;
    mo2["name"] = "optical"; mo2["ref"] = optical; mixedOk.append( mo2 );
    ms2["name"] = "sar"; ms2["ref"] = vhA; mixedOk.append( ms2 );
    CHECK( preflightRefs( "flood", mixedOk )["verdict"].asString() != "blocked" );

    // A clean same-pol pair instantiates and compiles (Tier A: the real SAR
    // change operators run in their own suites; the eval grades the harness
    // contract — preflight gates, recipe slots, compilation).
    Json::Value bindings;
    bindings["slots"]["primary"] = vhA;
    bindings["slots"]["reference"] = vhB;
    bindings["output_dir"] = tmp.path().toStdString();
    Json::Value instInput;
    instInput["recipe_id"] = "harness.sar_flood_vh";
    instInput["bindings"] = bindings;
    const SpatialToolResult instantiated = callTool( "harness:instantiate_recipe", instInput );
    REQUIRE( instantiated.success );
    AgentPlan parsed;
    HarnessError error;
    REQUIRE( readAgentPlan( instantiated.output["plan"], parsed, error ) );
    CHECK( compilePlanToWorkflowJson( parsed, error ).size() > 0 );

    // The collapsed VV variant instantiates through the preset with the same
    // scientific gates (de-duplication regression pin).
    Json::Value bindingsVv = bindings;
    bindingsVv["preset"] = "vv";
    Json::Value instVv;
    instVv["recipe_id"] = "harness.sar_flood_vh";
    instVv["bindings"] = bindingsVv;
    const SpatialToolResult instVvResult = callTool( "harness:instantiate_recipe", instVv );
    REQUIRE( instVvResult.success );
    bool sawVv = false;
    for ( const auto &step : instVvResult.output["plan"]["steps"] )
        if ( step["params"].get( "polarizations", "" ).asString() == "VV" )
            sawVv = true;
    CHECK( sawVv );
}

TEST_CASE( "Eval: DEM terrain full pipeline", "[harness][eval][terrain]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string dem = writeDemRaster( tmp.filePath( "dem.tif" ) );

    // Terrain preflight passes a projected-CRS DEM.
    Json::Value refs;
    Json::Value r;
    r["name"] = "primary"; r["ref"] = dem; refs.append( r );
    CHECK( preflightRefs( "terrain", refs )["verdict"].asString() != "blocked" );

    // Full execution (Tier B) on the real terrain operator.
    Json::Value plan;
    plan["schema_version"] = "2.0";
    plan["kind"] = "execution_plan";
    plan["plan_id"] = "plan-terrain-eval";
    plan["goal"] = "Slope from DEM";
    plan["intent"] = "terrain";
    Json::Value steps( Json::arrayValue );
    Json::Value slope;
    slope["id"] = "slope";
    slope["operator_id"] = "rs:terrain_analysis";
    slope["params"]["input"] = dem;
    slope["params"]["output"] = tmp.filePath( "slope.tif" ).toStdString();
    slope["params"]["product"] = "slope";
    slope["verification"] = "raster";
    steps.append( slope );
    plan["steps"] = steps;
    Json::Value outputs( Json::arrayValue );
    Json::Value out;
    out["name"] = "slope"; out["from_step"] = "slope"; out["port"] = "output"; out["kind"] = "raster";
    outputs.append( out );
    plan["outputs"] = outputs;

    const Json::Value status = executePlanToTerminal( plan );
    CHECK( status["status"].asString() == "completed" );
    CHECK( status["verification"]["verdict"].asString() != "FAIL" );
}

TEST_CASE( "Eval: temporal trend pack enforces scene facts", "[harness][eval][temporal]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string scene = writeOpticalRaster( tmp.filePath( "t0.tif" ) );
    auto temporalFacts = [ & ]( int sceneCount, bool sortedDates ) {
        Json::Value refs;
        Json::Value r;
        r["name"] = "primary";
        r["ref"] = scene;
        Json::Value facts;
        facts["scene_count"] = sceneCount;
        facts["dates"].append( sortedDates ? "2025-01-01" : "2025-03-01" );
        facts["dates"].append( sortedDates ? "2025-02-01" : "2025-01-01" );
        r["temporal_facts"] = facts;
        refs.append( r );
        return preflightRefs( "temporal", refs );
    };

    // Facts below the series floor block.
    CHECK( hasBlocker( temporalFacts( 2, true ), "INVALID_PARAMETER" ) );
    // Unsorted dates block.
    CHECK( hasBlocker( temporalFacts( 8, false ), "TIME_ORDER_INVALID" ) );
    // Sorted, sufficient scenes: no blockers.
    CHECK( !hasBlocker( temporalFacts( 8, true ), "INVALID_PARAMETER" ) );
    CHECK( !hasBlocker( temporalFacts( 8, true ), "TIME_ORDER_INVALID" ) );

    // Phenology demands more scenes: 5 facts block the 12-scene floor.
    Json::Value refs;
    Json::Value r;
    r["name"] = "primary";
    r["ref"] = scene;
    Json::Value facts;
    facts["scene_count"] = 5;
    r["temporal_facts"] = facts;
    refs.append( r );
    CHECK( hasBlocker( preflightRefs( "phenology", refs ), "INVALID_PARAMETER" ) );

    // No facts at all: warning only, never a fake block.
    Json::Value bare;
    Json::Value rb;
    rb["name"] = "primary"; rb["ref"] = scene; bare.append( rb );
    CHECK( preflightRefs( "temporal", bare )["verdict"].asString() != "blocked" );
}

TEST_CASE( "Eval: inference preflight verifies the model contract", "[harness][eval][inference]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string optical = writeOpticalRaster( tmp.filePath( "inf_scene.tif" ) );

    // Unknown model: typed MODEL_NOT_READY blocker.
    Json::Value refs;
    Json::Value r;
    r["name"] = "primary";
    r["ref"] = optical;
    r["model"] = "definitely-not-a-model";
    refs.append( r );
    CHECK( hasBlocker( preflightRefs( "inference", refs ), "MODEL_NOT_READY" ) );

    // No model declared: compatibility unverified -> warning, not a block.
    Json::Value bare;
    Json::Value rb;
    rb["name"] = "primary"; rb["ref"] = optical; bare.append( rb );
    CHECK( preflightRefs( "inference", bare )["verdict"].asString() != "blocked" );
}

TEST_CASE( "Eval: bounded plan repair fixes structure and stays advisory on science",
           "[harness][eval][repair]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();

    // Duplicate step ids + an output referencing an unknown step: both are
    // deterministically repairable.
    Json::Value plan;
    Json::Value steps( Json::arrayValue );
    Json::Value s1, s2;
    s1["id"] = "ndvi"; s1["operator_id"] = "rs:ndvi";
    s1["params"]["red"] = "/x.tif"; s1["params"]["nir"] = "/x.tif";
    s1["params"]["output"] = "/y.tif";
    s2 = s1; // duplicate id
    steps.append( s1 );
    steps.append( s2 );
    plan["schema_version"] = "2.0";
    plan["kind"] = "execution_plan";
    plan["plan_id"] = "plan-repair-eval";
    plan["intent"] = "ndvi";
    plan["steps"] = steps;
    Json::Value outputs( Json::arrayValue );
    Json::Value good, dangling;
    good["name"] = "a"; good["from_step"] = "ndvi"; good["port"] = "output"; good["kind"] = "raster";
    dangling["name"] = "b"; dangling["from_step"] = "ghost"; dangling["port"] = "output"; dangling["kind"] = "raster";
    outputs.append( good );
    outputs.append( dangling );
    plan["outputs"] = outputs;

    Json::Value input;
    input["plan"] = plan;
    const SpatialToolResult repaired = callTool( "harness:repair_plan", input );
    REQUIRE( repaired.success );
    CHECK( repaired.output["compilable"].asBool() );
    CHECK( repaired.output["repair_log"].size() >= 1 );
    CHECK( repaired.output["workflow_json"].asString().size() > 0 );

    // Science is never auto-edited: an unknown operator stays advisory.
    Json::Value bad;
    bad["schema_version"] = "2.0";
    bad["kind"] = "execution_plan";
    bad["plan_id"] = "plan-repair-eval-2";
    Json::Value steps2( Json::arrayValue );
    Json::Value s;
    s["id"] = "magic";
    s["operator_id"] = "rs:teleportation";
    steps2.append( s );
    bad["steps"] = steps2;
    Json::Value input2;
    input2["plan"] = bad;
    const SpatialToolResult advisory = callTool( "harness:repair_plan", input2 );
    REQUIRE( advisory.success );
    CHECK( !advisory.output["compilable"].asBool() );
    CHECK( advisory.output["issues"].size() >= 1 );
    bool suggestsSearch = false;
    for ( const auto &issue : advisory.output["issues"] )
        for ( const auto &action : issue["suggested_actions"] )
            if ( action["action"].asString() == "search_capabilities" )
                suggestsSearch = true;
    CHECK( suggestsSearch );
}

TEST_CASE( "Eval: intent ambiguity resolves through typed candidates and decisions",
           "[harness][eval][ambiguity]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    RecipeCatalog::instance().setDirectory( ( std::filesystem::path( CMAKE_SOURCE_DIR ) /
                                              "data/agent/recipes" ).string() );

    // Competing evidence -> typed ambiguity with candidates, never a guess.
    Json::Value ambiguous;
    ambiguous["goal"] = "water change";
    const SpatialToolResult ambiguousResult = callTool( "harness:resolve_intent", ambiguous );
    REQUIRE( ambiguousResult.success );
    CHECK( ambiguousResult.output["status"].asString() == "ambiguous" );
    CHECK( ambiguousResult.output["candidates"].size() >= 2 );
    CHECK( ambiguousResult.output["ambiguity"]["code"].asString() == "INTENT_AMBIGUOUS" );

    // Clear evidence resolves deterministically.
    Json::Value clear;
    clear["goal"] = "Sentinel-2 NDVI 植被指数";
    const SpatialToolResult clearResult = callTool( "harness:resolve_intent", clear );
    REQUIRE( clearResult.success );
    CHECK( clearResult.output["intent"].asString() == "ndvi" );
    CHECK( clearResult.output["capabilities"]["total"].asInt() >= 1 );

    // No evidence -> typed unresolved (inspect instead of guess).
    Json::Value nothing;
    nothing["goal"] = "hello world";
    CHECK( callTool( "harness:resolve_intent", nothing ).output["status"].asString()
           == "unresolved" );

    // Decision ledger: record -> surfaces in context -> resolve.
    Json::Value record;
    record["action"] = "record";
    record["kind"] = "ambiguity";
    record["subject"] = "water change goal";
    record["note"] = "optical NDWI or SAR change?";
    const SpatialToolResult recorded = callTool( "harness:decision_record", record );
    REQUIRE( recorded.success );
    const std::string decisionId = recorded.output["decision_id"].asString();

    Json::Value list;
    list["action"] = "list";
    const SpatialToolResult listed = callTool( "harness:decision_record", list );
    REQUIRE( listed.success );
    bool surfaced = false;
    for ( const auto &decision : listed.output["decisions"] )
        if ( decision["id"].asString() == decisionId &&
             decision["status"].asString() == "unresolved" )
            surfaced = true;
    CHECK( surfaced );

    const SpatialToolResult context = callTool( "harness:context", Json::Value() );
    REQUIRE( context.success );
    bool inContext = false;
    for ( const auto &decision : context.output["context"]["decisions"] )
        if ( decision["id"].asString() == decisionId )
            inContext = true;
    CHECK( inContext );

    Json::Value resolve;
    resolve["action"] = "resolve";
    resolve["decision_id"] = decisionId;
    resolve["chosen"] = "optical-ndwi";
    CHECK( callTool( "harness:decision_record", resolve ).output["resolved"].asBool() );
}

TEST_CASE( "Eval: understanding cache and plan binding persist typed context",
           "[harness][eval][continuity]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    SpatialToolRegistry::instance().registerBuiltinTools();

    const std::string raster = writeOpticalRaster( tmp.filePath( "cache_scene.tif" ) );

    Json::Value understand;
    understand["asset"] = raster;
    const SpatialToolResult first = callTool( "spatial:understand", understand );
    REQUIRE( first.success );
    CHECK( first.output["cached"].asBool() == false );
    const SpatialToolResult second = callTool( "spatial:understand", understand );
    REQUIRE( second.success );
    CHECK( second.output["cached"].asBool() == true );
    // Same facts either way.
    CHECK( second.output["dataset_understanding"]["modality"].asString()
           == first.output["dataset_understanding"]["modality"].asString() );

    // A full run binds itself into the context with its verification status.
    RecipeCatalog::instance().setDirectory( ( std::filesystem::path( CMAKE_SOURCE_DIR ) /
                                              "data/agent/recipes" ).string() );
    Json::Value bindings;
    bindings["slots"]["primary"] = raster;
    bindings["output_dir"] = tmp.path().toStdString();
    Json::Value instInput;
    instInput["recipe_id"] = "harness.optical_ndvi";
    instInput["bindings"] = bindings;
    const SpatialToolResult instantiated = callTool( "harness:instantiate_recipe", instInput );
    REQUIRE( instantiated.success );
    const Json::Value status = executePlanToTerminal( instantiated.output["plan"] );
    REQUIRE( status["status"].asString() == "completed" );

    const SpatialToolResult context = callTool( "harness:context", Json::Value() );
    REQUIRE( context.success );
    bool bindingFound = false;
    for ( const auto &binding : context.output["context"]["plan_bindings"] )
    {
        if ( binding["verification_status"].asString() == "PASS" )
            bindingFound = true;
    }
    CHECK( bindingFound );
}

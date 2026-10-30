// tests/test_r26_harness_evidence_integrity.cpp
//
// R26-harness: evidence closed-loop integrity oracles.
//   A1 — a verification-evidence sidecar that cannot be persisted fails the
//        run CLOSED (status failed), never a "completed" claim whose evidence
//        is missing.
//   A2 — re-executing an output path with a new run replaces the stale
//        harness provenance identity; an engine DerivationRecord sidecar is
//        never touched.
//   A3 — a corrupt verification sidecar for THIS run surfaces as a typed
//        integrity failure and is not silently recomputed/rewritten (polls
//        must never re-flip a verdict).
//   B  — harness:workflow_session delete failures are typed tool failures on
//        the production SpatialToolRegistry surface.
//   C  — rebinding a run refreshes the binding's plan identity (no stale
//        plan_id after a re-plan on the same run).

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <json/json.h>
#include <json/reader.h>

#include "agent/harness/context_checkpoint.h"
#include "agent/harness/context_ledger.h"
#include "agent/harness/evidence.h"
#include "agent/harness/plan_tools.h"

#include "agent/spatial_tools/spatial_tool.h"

#include <workflow/workflow_run.h>

#include <gdal_priv.h>

#include <fstream>
#include <memory>

using namespace sicnu::agent::harness;

namespace
{

/// Minimal valid single-band raster so verifyArtifact exercises real GDAL
/// paths (bounded 8x8) — same contract as test_harness_evidence.cpp.
std::string writeTinyRaster( const QString &path )
{
    static const bool kRegistered = [] {
        GDALAllRegister();
        return true;
    }();
    ( void )kRegistered;
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    if ( !driver )
        return {};
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    if ( !ds )
        return {};
    double geoTransform[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
    ds->SetGeoTransform( geoTransform );
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32650 ) == OGRERR_NONE )
    {
        char *wkt = nullptr;
        srs.exportToWkt( &wkt );
        ds->SetProjection( wkt );
        CPLFree( wkt );
    }
    float row[8] = {};
    for ( int y = 0; y < 8; ++y )
    {
        for ( int x = 0; x < 8; ++x )
            row[x] = static_cast<float>( x + y ) / 16.0f;
        ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, 8, 1, row, 8, 1, GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return path.toStdString();
}

std::shared_ptr<sicnu::workflow::WorkflowRun> completedRun( const std::string &runId,
                                                            const std::string &outputPath )
{
    using namespace sicnu::workflow;
    auto run = std::make_shared<WorkflowRun>();
    REQUIRE( run->setRunId( runId ) );
    StepPlan step;
    step.stepId = "s1";
    step.operatorId = "rs:contrast_stretch";
    step.status = "Completed";
    step.outputLayerPath = outputPath;
    run->setStepPlans( { step } );
    run->forceSetState( WorkflowRunState::Completed );
    return run;
}

Json::Value readJsonFile( const std::string &path )
{
    QFile file( QString::fromStdString( path ) );
    if ( !file.open( QIODevice::ReadOnly ) )
        return {};
    const QByteArray raw = file.readAll();
    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &doc, &errors ) )
        return {};
    return doc;
}

std::string readRawFile( const std::string &path )
{
    QFile file( QString::fromStdString( path ) );
    if ( !file.open( QIODevice::ReadOnly ) )
        return {};
    return std::string( file.readAll().constData() );
}

bool writeFileRaw( const std::string &path, const std::string &bytes )
{
    QFile file( QString::fromStdString( path ) );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    return file.write( bytes.data(), static_cast<qint64>( bytes.size() ) )
           == static_cast<qint64>( bytes.size() );
}

} // namespace

TEST_CASE( "A1: a verification sidecar that cannot be written fails the run closed",
           "[harness][r26][evidence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string raster =
      writeTinyRaster( dir.filePath( QStringLiteral( "a1.tif" ) ) );
    REQUIRE( QFileInfo::exists( QString::fromStdString( raster ) ) );

    // Block ONLY the verification sidecar lane: a directory at the sidecar
    // path makes the atomic publish fail while everything else stays
    // writable.
    const QString sidecarDir = QString::fromStdString( raster + ".verification.json" );
    REQUIRE( QDir().mkpath( sidecarDir ) );

    const auto run = completedRun( "run-a1", raster );
    const Json::Value doc = runResultDocument( run, nullptr, true );

    CHECK( doc["status"].asString() == "failed" );
    CHECK( doc["verification"]["verdict"].asString() == "FAIL" );
    bool foundSeverity = false;
    bool severityIsError = false;
    for ( const Json::Value &artifact : doc["verification"]["artifacts"] )
        for ( const Json::Value &check : artifact["checks"] )
            if ( check.get( "check", "" ).asString() == "verification_evidence_written"
                 && !check.get( "passed", true ).asBool() )
            {
                foundSeverity = true;
                severityIsError = check.get( "severity", "" ).asString() == "error";
            }
    CHECK( foundSeverity );
    CHECK( severityIsError );
}

TEST_CASE( "A2: a re-executed output path carries the current run's provenance",
           "[harness][r26][evidence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string raster =
      writeTinyRaster( dir.filePath( QStringLiteral( "a2.tif" ) ) );
    REQUIRE( QFileInfo::exists( QString::fromStdString( raster ) ) );
    const std::string sidecar = raster + ".provenance.json";

    SECTION( "harness provenance follows the latest run" )
    {
        const auto runA = completedRun( "run-a2-first", raster );
        const Json::Value docA = runResultDocument( runA, nullptr, true );
        REQUIRE( docA["status"].asString() == "completed" );
        const Json::Value provenanceA = readJsonFile( sidecar );
        REQUIRE( provenanceA.get( "kind", "" ).asString() == "harness_provenance" );
        CHECK( provenanceA["run"].get( "run_id", "" ).asString() == "run-a2-first" );

        // The output is re-executed (artifact bytes replaced) by a NEW run.
        REQUIRE( writeTinyRaster( QString::fromStdString( raster ) ).empty() == false );
        const auto runB = completedRun( "run-a2-second", raster );
        const Json::Value docB = runResultDocument( runB, nullptr, true );
        REQUIRE( docB["status"].asString() == "completed" );

        const Json::Value provenanceB = readJsonFile( sidecar );
        CHECK( provenanceB.get( "kind", "" ).asString() == "harness_provenance" );
        CHECK( provenanceB["run"].get( "run_id", "" ).asString() == "run-a2-second" );
    }

    SECTION( "an engine DerivationRecord sidecar is never overwritten" )
    {
        // Engine-shaped provenance (no "kind" envelope; DerivationRecord
        // keys) pre-exists from the workflow runtime's own writer.
        const std::string engineSidecar = R"({"algorithmId":"rs:contrast_stretch",)"
                                          R"("algorithmVersion":"1.0",)"
                                          R"("parameters":{},)"
                                          R"("inputs":[],)"
                                          R"("taskReference":"task-7"})";
        REQUIRE( writeFileRaw( sidecar, engineSidecar ) );

        const auto runA = completedRun( "run-a2-engine", raster );
        const Json::Value doc = runResultDocument( runA, nullptr, true );
        REQUIRE( doc["status"].asString() == "completed" );

        // Byte-identical: the harness must not have replaced engine lineage.
        CHECK( readRawFile( sidecar ) == engineSidecar );
    }
}

TEST_CASE( "A3: a corrupt verification sidecar for this run fails closed",
           "[harness][r26][evidence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string raster =
      writeTinyRaster( dir.filePath( QStringLiteral( "a3.tif" ) ) );
    REQUIRE( QFileInfo::exists( QString::fromStdString( raster ) ) );
    const std::string sidecar = raster + ".verification.json";

    const auto run = completedRun( "run-a3", raster );
    const Json::Value firstPoll = runResultDocument( run, nullptr, true );
    REQUIRE( firstPoll["status"].asString() == "completed" );

    // Corrupt the persisted evidence for THIS run (bitrot / torn write).
    const std::string corrupt = "NOT JSON { run-a3";
    REQUIRE( writeFileRaw( sidecar, corrupt ) );

    const Json::Value secondPoll = runResultDocument( run, nullptr, true );

    // Fail closed: a completed claim without intact evidence is refused, and
    // the poll never silently re-evaluates into a fresh verdict.
    CHECK( secondPoll["status"].asString() == "failed" );
    CHECK( secondPoll["verification"]["verdict"].asString() == "FAIL" );
    bool integrityReported = false;
    for ( const Json::Value &artifact : secondPoll["verification"]["artifacts"] )
        for ( const Json::Value &check : artifact["checks"] )
            if ( check.get( "check", "" ).asString() == "verification_evidence_integrity"
                 && !check.get( "passed", true ).asBool()
                 && check.get( "severity", "" ).asString() == "error" )
                integrityReported = true;
    CHECK( integrityReported );

    // The corrupt bytes stay in place for audit — no silent rewrite.
    CHECK( readRawFile( sidecar ) == corrupt );
}

TEST_CASE( "B: workflow_session delete failures are typed failures on the tool surface",
           "[harness][r26][session]" )
{
    registerContextSessionTools();
    const auto tool =
      sicnu::agent::spatial_tools::SpatialToolRegistry::instance().find( "harness:workflow_session" );
    REQUIRE( tool.has_value() );

    Json::Value input( Json::objectValue );
    input["action"] = "delete";
    input["session_id"] = "r26-no-such-session";

    const sicnu::agent::spatial_tools::SpatialToolResult result = ( *tool )->execute( input );

    // A delete that did not happen must not read as tool success: agents
    // keying on success would believe the checkpoint is gone.
    CHECK_FALSE( result.success );
    CHECK_FALSE( result.errorCode.empty() );
}

TEST_CASE( "C: rebinding a run refreshes the binding's plan identity",
           "[harness][r26][ledger]" )
{
    ContextLedger &ledger = ContextLedger::instance();
    ledger.recordPlanBinding( "run-r26-c", "plan-1", "goal-1", "ndvi", "pending", "fp-1" );

    // The run is re-planned (same run id, new plan) and completes.
    ledger.recordPlanBinding( "run-r26-c", "plan-2", "goal-2", "ndvi", "PASS", "fp-2" );

    const Json::Value bindings = ledger.planBindings();
    bool found = false;
    for ( const Json::Value &binding : bindings )
        if ( binding.get( "run_id", "" ).asString() == "run-r26-c" )
        {
            found = true;
            CHECK( binding.get( "plan_id", "" ).asString() == "plan-2" );
            CHECK( binding.get( "goal", "" ).asString() == "goal-2" );
            CHECK( binding.get( "plan_fingerprint", "" ).asString() == "fp-2" );
        }
    CHECK( found );

    // Status-only updates (empty plan identity) keep the existing identity —
    // the "verification status changes as the run completes" contract.
    ledger.recordPlanBinding( "run-r26-c", "", "", "", "PASS_WITH_WARNINGS", "" );
    for ( const Json::Value &binding : ledger.planBindings() )
        if ( binding.get( "run_id", "" ).asString() == "run-r26-c" )
        {
            CHECK( binding.get( "plan_id", "" ).asString() == "plan-2" );
            CHECK( binding.get( "verification_status", "" ).asString() == "PASS_WITH_WARNINGS" );
        }
}

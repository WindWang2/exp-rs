// tests/test_harness_evidence.cpp
//
// Harness 8.0 (Areas E/F/G): evidence sidecars, plan pins, and the
// deterministic plan fingerprint. Unit-level, deterministic, runtime-written
// fixtures only.

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>

#include <json/json.h>
#include <json/reader.h>

#include "agent/harness/agent_plan.h"
#include "agent/harness/evidence.h"
#include "agent/harness/harness_verification.h"
#include "agent/harness/plan_tools.h"

#include <workflow/workflow_run.h>

#include <gdal_priv.h>

#include <fstream>

using namespace sicnu::agent::harness;

namespace {

/// Minimal valid single-band raster so verifyArtifact exercises real GDAL
/// paths (bounded 8x8).
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

Json::Value parseFile( const std::string &path )
{
    QFile file( QString::fromStdString( path ) );
    if ( !file.open( QIODevice::ReadOnly ) )
        return Json::Value();
    const QByteArray raw = file.readAll();
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &parsed, &errors ) )
        return Json::Value();
    return parsed;
}

} // namespace

TEST_CASE( "plan fingerprint is deterministic and science-sensitive",
           "[harness][evidence8]" )
{
    Json::Value docA( Json::objectValue );
    docA["schema_version"] = "2.0";
    docA["kind"] = "execution_plan";
    docA["plan_id"] = "plan-a";
    docA["intent"] = "ndvi";
    Json::Value stepsA( Json::arrayValue );
    Json::Value step( Json::objectValue );
    step["id"] = "s1";
    step["operator_id"] = "rs:ndvi";
    step["params"]["scale"] = 1.0;
    stepsA.append( step );
    docA["steps"] = stepsA;

    AgentPlan a;
    HarnessError error;
    REQUIRE( readAgentPlan( docA, a, error ) );
    const std::string fingerprintA = planFingerprint( a );

    // Same science, different plan id: identical fingerprint.
    docA["plan_id"] = "plan-renamed";
    AgentPlan renamed;
    REQUIRE( readAgentPlan( docA, renamed, error ) );
    CHECK( planFingerprint( renamed ) == fingerprintA );

    // Changed science: different fingerprint.
    Json::Value docB = docA;
    docB["steps"][0]["params"]["scale"] = 0.0001;
    AgentPlan b;
    REQUIRE( readAgentPlan( docB, b, error ) );
    CHECK( planFingerprint( b ) != fingerprintA );
}

TEST_CASE( "compiled workflow carries run metadata (cleanup, pins, fingerprint)",
           "[harness][evidence8]" )
{
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = "2.0";
    doc["kind"] = "execution_plan";
    doc["plan_id"] = "plan-meta";
    doc["intent"] = "ndvi";
    doc["cleanup"] = "keep_outputs";
    Json::Value pins( Json::objectValue );
    Json::Value datasets( Json::objectValue );
    Json::Value pin( Json::objectValue );
    pin["asset_entity_id"] = "asset-7";
    datasets["primary"] = pin;
    pins["datasets"] = datasets;
    doc["pins"] = pins;
    Json::Value steps( Json::arrayValue );
    Json::Value step( Json::objectValue );
    step["id"] = "s1";
    step["operator_id"] = "rs:ndvi";
    steps.append( step );
    doc["steps"] = steps;
    doc["inputs"] = Json::Value( Json::arrayValue );
    Json::Value input( Json::objectValue );
    input["name"] = "primary";
    input["ref"] = "asset-7";
    doc["inputs"].append( input );

    AgentPlan plan;
    HarnessError error;
    REQUIRE( readAgentPlan( doc, plan, error ) );
    REQUIRE( validateAgentPlan( plan ).empty() );
    HarnessError compileError;
    const std::string workflowJson = compilePlanToWorkflowJson( plan, compileError );
    REQUIRE_FALSE( workflowJson.empty() );

    Json::Value compiled;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( workflowJson.c_str(), workflowJson.c_str() + workflowJson.size(),
                            &compiled, &errors ) );
    CHECK( compiled["metadata"]["cleanup"].asString() == "keep_outputs" );
    CHECK( compiled["metadata"]["plan_fingerprint"].asString() == planFingerprint( plan ) );
    CHECK( compiled["metadata"]["pins"]["datasets"]["primary"]["asset_entity_id"].asString()
           == "asset-7" );
}

TEST_CASE( "plan pins refuse mismatched input identity", "[harness][evidence8]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string raster = writeTinyRaster( dir.filePath( QStringLiteral( "scene.tif" ) ) );

    Json::Value doc( Json::objectValue );
    doc["schema_version"] = "2.0";
    doc["kind"] = "execution_plan";
    doc["plan_id"] = "plan-pins";
    Json::Value steps( Json::arrayValue );
    Json::Value step( Json::objectValue );
    step["id"] = "s1";
    step["operator_id"] = "rs:ndvi";
    steps.append( step );
    doc["steps"] = steps;
    Json::Value input( Json::objectValue );
    input["name"] = "primary";
    input["ref"] = raster;
    doc["inputs"].append( input );
    Json::Value pins( Json::objectValue );
    Json::Value datasets( Json::objectValue );
    Json::Value pin( Json::objectValue );
    pin["path"] = raster;
    datasets["primary"] = pin;
    pins["datasets"] = datasets;
    doc["pins"] = pins;

    AgentPlan plan;
    HarnessError error;
    REQUIRE( readAgentPlan( doc, plan, error ) );

    // Pin matches the resolved input: identity holds.
    CHECK( validatePlanIdentity( plan ).code.empty() );

    // Pin to a different path: typed identity mismatch.
    Json::Value swappedPin( Json::objectValue );
    swappedPin["path"] = dir.filePath( QStringLiteral( "other.tif" ) ).toStdString();
    plan.pins["datasets"]["primary"] = swappedPin;
    const HarnessError mismatch = validatePlanIdentity( plan );
    CHECK( mismatch.code == "IDENTITY_MISMATCH" );
    CHECK( mismatch.recoverable );
}

TEST_CASE( "uncertainty sidecars are written only from operator-declared facts",
           "[harness][evidence8]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string raster = writeTinyRaster( dir.filePath( QStringLiteral( "rx_out.tif" ) ) );

    sicnu::workflow::StepPlan step;
    step.stepId = "detect";
    step.operatorId = "rs:rx_anomaly";
    step.outputLayerPath = raster;
    step.resultPayload["uncertainty"]["threshold"] = 3.5;
    step.resultPayload["uncertainty"]["method"] = "rx";

    const std::vector<sicnu::workflow::StepPlan> steps{ step };
    const evidence::UncertaintyHarvest harvest =
        evidence::harvestUncertainty( steps, raster );
    CHECK( harvest.declared );

    // No fabrication for an artifact no step declared uncertainty about.
    const evidence::UncertaintyHarvest none =
        evidence::harvestUncertainty( steps, "/other/path.tif" );
    CHECK_FALSE( none.declared );
    const evidence::SidecarResult skipped = evidence::writeUncertaintySidecar(
        "/other/path.tif", none );
    CHECK_FALSE( skipped.written );
    CHECK( skipped.error.empty() );
    CHECK_FALSE( QFileInfo::exists( QString::fromStdString( "/other/path.tif" ) ) );

    const evidence::SidecarResult written =
        evidence::writeUncertaintySidecar( raster, harvest );
    REQUIRE( written.written );
    CHECK( written.path == raster + ".uncertainty.json" );

    const Json::Value sidecar = parseFile( written.path );
    CHECK( sidecar["kind"].asString() == "uncertainty_sidecar" );
    CHECK( sidecar["source"].asString() == "operator_declared" );
    CHECK( sidecar["facts"][0]["operator_id"].asString() == "rs:rx_anomaly" );

    // Verification now sees the sidecar the harness wrote.
    VerificationExpectations expectations;
    expectations.requireUncertainty = true;
    const ArtifactVerification verified = verifyArtifact( raster, expectations );
    bool uncertaintyPresent = false;
    for ( const VerificationCheck &check : verified.checks )
        uncertaintyPresent |= check.check == "uncertainty_present" && check.passed;
    CHECK( uncertaintyPresent );
}

TEST_CASE( "run-identity provenance never overwrites the engine sidecar",
           "[harness][evidence8]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string artifact = dir.filePath( QStringLiteral( "product.tif" ) ).toStdString();
    {
        std::ofstream stream( artifact, std::ios::binary );
        stream << "placeholder";
    }

    Json::Value identity( Json::objectValue );
    identity["run_id"] = "run-1";
    identity["plan_fingerprint"] = "abcdef0123456789";

    const evidence::SidecarResult written =
        evidence::writeProvenanceSidecarIfAbsent( artifact, identity );
    REQUIRE( written.written );

    // Engine-style sidecar already present: harness stays hands-off.
    const evidence::SidecarResult second =
        evidence::writeProvenanceSidecarIfAbsent( artifact, identity );
    CHECK_FALSE( second.written );
    CHECK( second.error.empty() );

    const Json::Value sidecar = parseFile( artifact + ".provenance.json" );
    CHECK( sidecar["kind"].asString() == "harness_provenance" );
    CHECK( sidecar["run"]["run_id"].asString() == "run-1" );
}

TEST_CASE( "verification evidence sidecar records verdict and quality",
           "[harness][evidence8]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string raster = writeTinyRaster( dir.filePath( QStringLiteral( "final.tif" ) ) );

    VerificationExpectations expectations;
    const ArtifactVerification verified = verifyArtifact( raster, expectations );
    REQUIRE( verified.verdict != Verdict::Fail );

    Json::Value identity( Json::objectValue );
    identity["run_id"] = "run-2";
    const evidence::UncertaintyHarvest noFacts;
    const evidence::SidecarResult written = evidence::writeVerificationEvidence(
        raster, verified, expectations, identity, noFacts );
    REQUIRE( written.written );

    const Json::Value evidence = parseFile( written.path );
    CHECK( evidence["kind"].asString() == "verification_evidence" );
    CHECK( evidence["verification"]["verdict"].asString() ==
           verdictToStringWire( verified.verdict ) );
    CHECK( evidence["quality"]["verdict"].asString() == verdictToStringWire( verified.verdict ) );
    CHECK( evidence["quality"]["uncertainty_declared"].asBool() == false );
    CHECK( evidence["run"]["run_id"].asString() == "run-2" );
}

TEST_CASE( "adversarial-review remediations hold", "[harness][evidence8][review]" )
{
    SECTION( "duplicate input slot names are rejected (pin bypass)" )
    {
        Json::Value doc( Json::objectValue );
        doc["schema_version"] = "2.0";
        doc["kind"] = "execution_plan";
        doc["plan_id"] = "plan-dup";
        Json::Value steps( Json::arrayValue );
        Json::Value step( Json::objectValue );
        step["id"] = "s1";
        step["operator_id"] = "rs:ndvi";
        steps.append( step );
        doc["steps"] = steps;
        Json::Value a( Json::objectValue );
        a["name"] = "primary";
        a["ref"] = "/data/a.tif";
        doc["inputs"].append( a );
        Json::Value b( Json::objectValue );
        b["name"] = "primary";
        b["ref"] = "/data/b.tif";
        doc["inputs"].append( b );

        AgentPlan plan;
        HarnessError error;
        REQUIRE( readAgentPlan( doc, plan, error ) );
        const auto issues = validateAgentPlan( plan );
        bool dupReported = false;
        for ( const auto &issue : issues )
            dupReported |= issue.error.summary.find( "Duplicate input slot name" )
                           != std::string::npos;
        CHECK( dupReported );
    }

    SECTION( "a non-object pins block is a typed error, never silently dropped" )
    {
        Json::Value doc( Json::objectValue );
        doc["schema_version"] = "2.0";
        doc["kind"] = "execution_plan";
        doc["plan_id"] = "plan-bad-pins";
        Json::Value steps( Json::arrayValue );
        Json::Value step( Json::objectValue );
        step["id"] = "s1";
        step["operator_id"] = "rs:ndvi";
        steps.append( step );
        doc["steps"] = steps;
        doc["pins"] = "primary=/data/a.tif";

        AgentPlan plan;
        HarnessError error;
        CHECK_FALSE( readAgentPlan( doc, plan, error ) );
        CHECK( error.code == "INVALID_PLAN" );
    }

    SECTION( "model pins that do not resolve are MODEL_NOT_READY" )
    {
        Json::Value doc( Json::objectValue );
        doc["schema_version"] = "2.0";
        doc["kind"] = "execution_plan";
        doc["plan_id"] = "plan-model-pin";
        Json::Value steps( Json::arrayValue );
        Json::Value step( Json::objectValue );
        step["id"] = "s1";
        step["operator_id"] = "rs:ndvi";
        steps.append( step );
        doc["steps"] = steps;
        doc["inputs"] = Json::Value( Json::arrayValue );
        Json::Value pins( Json::objectValue );
        pins["model"] = "no-such-model-in-catalog@1";
        doc["pins"] = pins;

        AgentPlan plan;
        HarnessError error;
        REQUIRE( readAgentPlan( doc, plan, error ) );
        const HarnessError mismatch = validatePlanIdentity( plan );
        CHECK( mismatch.code == "MODEL_NOT_READY" );
    }

    SECTION( "fingerprint covers pins and cleanup" )
    {
        Json::Value doc( Json::objectValue );
        doc["schema_version"] = "2.0";
        doc["kind"] = "execution_plan";
        doc["plan_id"] = "plan-fp";
        Json::Value steps( Json::arrayValue );
        Json::Value step( Json::objectValue );
        step["id"] = "s1";
        step["operator_id"] = "rs:ndvi";
        steps.append( step );
        doc["steps"] = steps;
        AgentPlan plan;
        HarnessError error;
        REQUIRE( readAgentPlan( doc, plan, error ) );
        const std::string base = planFingerprint( plan );

        plan.cleanup = "keep_outputs";
        CHECK( planFingerprint( plan ) != base );

        Json::Value pins( Json::objectValue );
        Json::Value datasets( Json::objectValue );
        Json::Value pin( Json::objectValue );
        pin["path"] = "/data/a.tif";
        datasets["primary"] = pin;
        pins["datasets"] = datasets;
        plan.pins = pins;
        CHECK( planFingerprint( plan ) != base );
    }

    SECTION( "verification evidence read-back is run-scoped" )
    {
        QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        const std::string raster = writeTinyRaster( dir.filePath( QStringLiteral( "reuse.tif" ) ) );
        VerificationExpectations expectations;
        const ArtifactVerification verified = verifyArtifact( raster, expectations );
        Json::Value identity( Json::objectValue );
        identity["run_id"] = "run-A";
        const evidence::UncertaintyHarvest noFacts;
        const auto written = evidence::writeVerificationEvidence( raster, verified, expectations,
                                                                  identity, noFacts );
        REQUIRE( written.written );

        // Same run: authoritative reuse. Different run: fresh evaluation.
        const auto reused = evidence::readVerificationEvidence( raster, "run-A" );
        REQUIRE( reused.has_value() );
        CHECK( reused->verdict == verified.verdict );
        CHECK( reused->checks.size() == verified.checks.size() );
        CHECK_FALSE( evidence::readVerificationEvidence( raster, "run-B" ).has_value() );
    }
}

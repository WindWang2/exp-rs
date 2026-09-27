// tests/test_grounding_evidence_r4.cpp — Track 8 R4 WP-D: the grounding /
// evidence acquisition oracle. Pure TEST-side (BASELINE.md §3: #1337 is
// in-flight on grounding_probes.cpp — no implementation changes here).
//
// Ground truth comes from PRE-RECORDED deterministic tables and the
// documented contracts, never from the implementation under test:
//  * harvestUncertainty: steps with hand-written result payloads; the
//    harvest must match the recorded table field-by-field, and only the
//    CLOSED key list (uncertainty | uncertaintyOutput | uncertainty_band |
//    confidence) may cross the boundary.
//  * probeDatasetFacts: the understanding body is PRE-SEEDED into the
//    shared ledger cache under the documented key convention — the probe
//    must answer from that table (source "cache") with the projected
//    fields; unknown scopes / unknown references are typed errors BEFORE
//    any I/O; a fact the body cannot answer stays "unknown", never a
//    fabricated value.
//  * probeModelManifest: an unknown model id is typed-unknown; the
//    artifact check is stat-only (presence + size, never a read).
// Float comparisons tolerate 1e-12 relative error (no float facts asserted
// here are computed — they round-trip verbatim).

#include <catch2/catch_test_macros.hpp>

#include "agent/harness/context_ledger.h"
#include "agent/harness/evidence.h"
#include "agent/harness/grounding_probes.h"
#include "agent/harness/grounding_tools.h"
#include "agent/harness/harness_verification.h"

#include <workflow/workflow_run.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <json/reader.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace sicnu::agent::harness;
using sicnu::workflow::StepPlan;

namespace {

/// Parses sidecar bytes (jsoncpp lane — evidence sidecars are jsoncpp).
Json::Value parseSidecar( const QByteArray &bytes )
{
    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &doc, &errors ) )
        return Json::Value();
    return doc;
}

/// A completed step declaring uncertainty facts for `output`.
StepPlan declaringStep( const std::string &id, const std::string &output )
{
    StepPlan step;
    step.stepId = id;
    step.operatorId = "rs:spectral_index";
    step.status = "Completed";
    step.outputLayerPath = output;
    Json::Value uncertainty( Json::objectValue );
    uncertainty["method"] = "error_matrix";
    uncertainty["overall_accuracy"] = 0.93;
    Json::Value confidence( Json::objectValue );
    confidence["grade"] = "B2";
    step.resultPayload["uncertainty"] = uncertainty;
    step.resultPayload["confidence"] = confidence;
    step.resultPayload["uncertainty_band"] = 2;
    step.resultPayload["NOT_AN_UNCERTAINTY_KEY"] = "must never cross";
    return step;
}

/// A completed step with a payload but no uncertainty keys at all.
StepPlan silentStep( const std::string &id, const std::string &output )
{
    StepPlan step;
    step.stepId = id;
    step.operatorId = "rs:spectral_index";
    step.status = "Completed";
    step.outputLayerPath = output;
    step.resultPayload["crs"] = "EPSG:32650";
    return step;
}

} // namespace

// — harvestUncertainty: the pre-recorded table ———————————————————————————

TEST_CASE( "the uncertainty harvest matches the recorded step payload "
           "field-by-field and drops foreign keys", "[harness][grounding-r4][harvest]" )
{
    QTemporaryDir dir;
    const std::string output = dir.filePath( "classified.gpkg" ).toStdString();

    const std::vector<StepPlan> steps = {
        silentStep( "prep", output ),     // no keys → never interpreted
        declaringStep( "classify", output ),
        declaringStep( "elsewhere", dir.filePath( "other.tif" ).toStdString() ), // other output
    };

    const evidence::UncertaintyHarvest harvest = evidence::harvestUncertainty( steps, output );
    CHECK( harvest.declared );
    REQUIRE( harvest.facts.size() == 1 ); // exactly the matching step

    const Json::Value &entry = harvest.facts[0];
    CHECK( entry["step_id"].asString() == "classify" );
    CHECK( entry["operator_id"].asString() == "rs:spectral_index" );
    const Json::Value &facts = entry["facts"];
    // Closed key list: exactly the declared keys, nothing else.
    REQUIRE( facts.size() == 3 );
    CHECK( facts["uncertainty"]["method"].asString() == "error_matrix" );
    CHECK( std::abs( facts["uncertainty"]["overall_accuracy"].asDouble() - 0.93 ) < 1e-12 );
    CHECK( facts["confidence"]["grade"].asString() == "B2" );
    CHECK( facts["uncertainty_band"].asInt() == 2 );
    CHECK_FALSE( facts.isMember( "NOT_AN_UNCERTAINTY_KEY" ) );
    CHECK_FALSE( facts.isMember( "crs" ) );
}

TEST_CASE( "a payload without uncertainty keys is honest absence, not a "
           "fabricated harvest", "[harness][grounding-r4][harvest]" )
{
    QTemporaryDir dir;
    const std::string output = dir.filePath( "plain.tif" ).toStdString();
    const evidence::UncertaintyHarvest harvest =
        evidence::harvestUncertainty( { silentStep( "s", output ) }, output );
    CHECK_FALSE( harvest.declared );
    CHECK( harvest.facts.empty() );
}

TEST_CASE( "the harvest is bounded at 16 matching steps", "[harness][grounding-r4][harvest][bounds]" )
{
    QTemporaryDir dir;
    const std::string output = dir.filePath( "busy.tif" ).toStdString();
    std::vector<StepPlan> steps;
    for ( int i = 0; i < 18; ++i )
        steps.push_back( declaringStep( "step-" + std::to_string( i ), output ) );
    const evidence::UncertaintyHarvest harvest = evidence::harvestUncertainty( steps, output );
    CHECK( static_cast<int>( harvest.facts.size() ) == 16 );
}

// — sidecars: writes, absence honesty, engine precedence —————————————————

TEST_CASE( "the uncertainty sidecar is written only when something was "
           "declared", "[harness][grounding-r4][sidecar]" )
{
    QTemporaryDir dir;
    const std::string output = dir.filePath( "product.tif" ).toStdString();

    // Honest absence: no declaration → no file, no error.
    const evidence::UncertaintyHarvest silent = evidence::harvestUncertainty( {}, output );
    const evidence::SidecarResult skipped = evidence::writeUncertaintySidecar( output, silent );
    CHECK_FALSE( skipped.written );
    CHECK( skipped.error.empty() );
    CHECK_FALSE( QFileInfo::exists( QString::fromStdString( output + ".uncertainty.json" ) ) );

    // Declared → the file lands and carries the harvest verbatim.
    const evidence::UncertaintyHarvest harvest =
        evidence::harvestUncertainty( { declaringStep( "s1", output ) }, output );
    const evidence::SidecarResult written = evidence::writeUncertaintySidecar( output, harvest );
    CHECK( written.written );
    CHECK( written.error.empty() );
    QFile sidecar( QString::fromStdString( output + ".uncertainty.json" ) );
    REQUIRE( sidecar.open( QIODevice::ReadOnly ) );
    const Json::Value doc = parseSidecar( sidecar.readAll() );
    REQUIRE( doc.isObject() );
    CHECK( doc["kind"].asString() == "uncertainty_sidecar" );
    CHECK( doc["source"].asString() == "operator_declared" );
    CHECK( doc["facts"][0]["step_id"].asString() == "s1" );
}

TEST_CASE( "the harness provenance sidecar never overwrites the engine's",
           "[harness][grounding-r4][sidecar]" )
{
    QTemporaryDir dir;
    const std::string output = dir.filePath( "engine_product.tif" ).toStdString();
    const QString sidecarPath = QString::fromStdString( output + ".provenance.json" );
    {
        QFile engineSidecar( sidecarPath );
        REQUIRE( engineSidecar.open( QIODevice::WriteOnly ) );
        engineSidecar.write( QByteArray( "{\"kind\":\"engine_derivation\",\"author\":\"engine\"}" ) );
    }

    Json::Value runIdentity( Json::objectValue );
    runIdentity["run_id"] = "run-engine-precedence";
    const evidence::SidecarResult result =
        evidence::writeProvenanceSidecarIfAbsent( output, runIdentity );
    // Intentionally skipped: the engine sidecar wins, the harness never
    // overwrites derivation lineage.
    CHECK_FALSE( result.written );
    QFile sidecar( sidecarPath );
    REQUIRE( sidecar.open( QIODevice::ReadOnly ) );
    CHECK( sidecar.readAll().contains( "\"author\":\"engine\"" ) );
}

TEST_CASE( "a sidecar write that cannot land reports a typed error and "
           "leaves the artifact untouched", "[harness][grounding-r4][sidecar]" )
{
    QTemporaryDir dir;
    // A FILE standing in for a directory: the sidecar path cannot exist.
    const QString blocker = dir.filePath( QStringLiteral( "blocker" ) );
    {
        QFile f( blocker );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "artifact bytes" ) );
    }
    const std::string output = blocker.toStdString() + "/out.tif";

    evidence::UncertaintyHarvest harvest;
    harvest.declared = true;
    harvest.facts.append( Json::Value( Json::objectValue ) );
    const evidence::SidecarResult result = evidence::writeUncertaintySidecar( output, harvest );
    CHECK_FALSE( result.written );
    CHECK_FALSE( result.error.empty() );
    // The "artifact" (the blocker file) is untouched.
    QFile f( blocker );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    CHECK( f.readAll() == QByteArray( "artifact bytes" ) );
}

// — verification evidence: write/read round-trip, fail-closed reads ———————

TEST_CASE( "verification evidence round-trips its verdict and checks for the "
           "same run and refuses a different run", "[harness][grounding-r4][evidence]" )
{
    QTemporaryDir dir;
    const std::string output = dir.filePath( "verified.tif" ).toStdString();

    ArtifactVerification verification;
    verification.path = output;
    verification.kind = "raster";
    verification.verdict = Verdict::PassWithWarnings;
    VerificationCheck okCheck;
    okCheck.check = "artifact_exists";
    okCheck.passed = true;
    okCheck.severity = "info";
    VerificationCheck warnCheck;
    warnCheck.check = "provenance_present";
    warnCheck.passed = false;
    warnCheck.severity = "warning";
    verification.checks = { okCheck, warnCheck };

    VerificationExpectations expectations;
    expectations.crs = "EPSG:32650";
    expectations.minFiniteFraction = 0.9;

    Json::Value runIdentity( Json::objectValue );
    runIdentity["run_id"] = "run-A";

    const evidence::SidecarResult written = evidence::writeVerificationEvidence(
        output, verification, expectations, runIdentity, evidence::UncertaintyHarvest{} );
    REQUIRE( written.written );

    // Same run: the first evaluation is authoritative and stable.
    const auto readBack = evidence::readVerificationEvidence( output, "run-A" );
    REQUIRE( readBack.has_value() );
    CHECK( readBack->verdict == Verdict::PassWithWarnings );
    REQUIRE( readBack->checks.size() == 2 );
    CHECK( readBack->checks[0].check == "artifact_exists" );
    CHECK( readBack->checks[1].severity == "warning" );

    // A different run's poll must re-evaluate, not read foreign evidence.
    CHECK_FALSE( evidence::readVerificationEvidence( output, "run-B" ).has_value() );
}

TEST_CASE( "verification evidence reads fail closed on absent, corrupt, "
           "foreign or malformed documents", "[harness][grounding-r4][evidence]" )
{
    QTemporaryDir dir;
    const std::string output = dir.filePath( "hostile.tif" ).toStdString();
    const QString sidecar = QString::fromStdString( output + ".verification.json" );

    // Absent.
    CHECK_FALSE( evidence::readVerificationEvidence( output, "run" ).has_value() );

    // Corrupt JSON.
    {
        QFile f( sidecar );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "{\"kind\": " ) ); // torn document
    }
    CHECK_FALSE( evidence::readVerificationEvidence( output, "run" ).has_value() );

    // Foreign kind.
    {
        QFile f( sidecar );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "{\"kind\":\"uncertainty_sidecar\"}" ) );
    }
    CHECK_FALSE( evidence::readVerificationEvidence( output, "run" ).has_value() );

    // Unknown verdict string: refuse rather than guess.
    {
        QFile f( sidecar );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "{\"kind\":\"verification_evidence\",\"run\":{\"run_id\":\"run\"},"
                             "\"verification\":{\"verdict\":\"PROBABLY_FINE\"}}" ) );
    }
    CHECK_FALSE( evidence::readVerificationEvidence( output, "run" ).has_value() );

    // A malformed check entry fails the whole read (no half-evidence).
    {
        QFile f( sidecar );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "{\"kind\":\"verification_evidence\",\"run\":{\"run_id\":\"run\"},"
                             "\"verification\":{\"verdict\":\"PASS\",\"checks\":[{\"passed\":true}]}}" ) );
    }
    CHECK_FALSE( evidence::readVerificationEvidence( output, "run" ).has_value() );
}

// — grounding probes: typed refusals and the cache oracle —————————————————

TEST_CASE( "probe limits are the machine mirror of ProbeLimits",
           "[harness][grounding-r4][probe]" )
{
    const Json::Value limits = probeLimits();
    CHECK( limits["default_deadline_ms"].asInt() == ProbeLimits::kDefaultDeadlineMs );
    CHECK( limits["max_scopes"].asInt() == ProbeLimits::kMaxScopes );
    CHECK( limits["max_scopes_in_facts"].asInt() == ProbeLimits::kMaxScopesInFacts );
    CHECK( limits["logical_byte_budget"].asInt64() == ProbeLimits::kLogicalByteBudget );
    // The scope table is closed and machine-derivable.
    const auto scopes = probe_scope::allProbeScopes();
    for ( const std::string &scope : scopes )
        CHECK( probe_scope::isKnownProbeScope( scope ) );
    CHECK_FALSE( probe_scope::isKnownProbeScope( "vibes" ) );
}

TEST_CASE( "unknown, empty and over-bounds scope requests fail typed before "
           "any I/O", "[harness][grounding-r4][probe]" )
{
    // Unknown scope: typed INVALID_PARAMETER regardless of the reference.
    ProbeRequest unknownScope;
    unknownScope.ref = "this-file-does-not-matter.tif";
    unknownScope.scopes = { "identity", "vibes" };
    const ProbeOutcome unknown = probeDatasetFacts( unknownScope );
    CHECK_FALSE( unknown.ok );
    CHECK( unknown.code == error_codes::kInvalidParameter );
    CHECK( unknown.summary.find( "vibes" ) != std::string::npos );

    // No scopes at all.
    ProbeRequest noScopes;
    noScopes.ref = "also-irrelevant.tif";
    const ProbeOutcome empty = probeDatasetFacts( noScopes );
    CHECK_FALSE( empty.ok );
    CHECK( empty.code == error_codes::kInvalidParameter );

    // Past the declared bound: refused without probing. Note the vocabulary
    // is CLOSED, so a distinct over-bounds list cannot exist — the bound is
    // reachable only through the deduped set, and duplicates are tolerated
    // by contract. Pinned: a duplicate-saturated request must NOT fail with
    // the scope-bound refusal (dedup happens before the bound check).
    ProbeRequest duplicates;
    duplicates.ref = "also-irrelevant.tif";
    for ( int i = 0; i < ProbeLimits::kMaxScopes + 5; ++i )
        duplicates.scopes.push_back( probe_scope::kIdentity );
    const ProbeOutcome deduped = probeDatasetFacts( duplicates );
    if ( !deduped.ok )
        CHECK( deduped.summary.find( "scope bound" ) == std::string::npos );
}

TEST_CASE( "an unresolvable dataset reference is a typed unknown, never a "
           "guessed fact", "[harness][grounding-r4][probe]" )
{
    QTemporaryDir dir;
    ProbeRequest request;
    request.ref = dir.filePath( "missing.tif" ).toStdString();
    request.scopes = { probe_scope::kIdentity };
    const ProbeOutcome outcome = probeDatasetFacts( request );
    CHECK_FALSE( outcome.ok );
    CHECK( outcome.code == error_codes::kDatasetNotFound );
    CHECK( outcome.facts.empty() );
}

TEST_CASE( "the probe answers from the pre-seeded cache table with "
           "projected fields intact", "[harness][grounding-r4][probe][oracle]" )
{
    QTemporaryDir dir;
    const QString raster = dir.filePath( QStringLiteral( "scene.tif" ) );
    {
        QFile f( raster );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "raster-bytes" ) );
    }

    // The pre-recorded understanding body — the ONLY source of truth here.
    Json::Value body( Json::objectValue );
    body["path"] = raster.toStdString();
    body["source_kind"] = "raster";
    body["driver"] = "GTiff";
    Json::Value size( Json::objectValue );
    size["width"] = 512;
    size["height"] = 256;
    body["size"] = size;
    body["band_count"] = 4;
    const QString key = understandingCacheKeyFor( raster, 0 );
    ContextLedger::instance().cacheUnderstanding( key, body );

    ProbeRequest request;
    request.ref = raster.toStdString();
    request.scopes = { probe_scope::kIdentity, probe_scope::kGrid, probe_scope::kBands };
    const ProbeOutcome outcome = probeDatasetFacts( request );
    REQUIRE( outcome.ok );
    CHECK( outcome.source == "cache" ); // answered from the table, no I/O
    CHECK( outcome.facts["path"].asString() == raster.toStdString() );
    CHECK( outcome.facts["driver"].asString() == "GTiff" );
    CHECK( outcome.facts["size"]["width"].asInt() == 512 );
    CHECK( outcome.facts["band_count"].asInt() == 4 );
    // A scope the body cannot answer stays unknown — no fabrication.
    const Json::Value &facts = outcome.facts;
    if ( facts.isMember( "crs_authid" ) )
        CHECK( facts["crs_authid"].isNull() );
    if ( facts["fact_status"].isMember( "crs_authid" ) )
        CHECK( facts["fact_status"]["crs_authid"].asString() == "unknown" );
}

TEST_CASE( "the model probe refuses empty and unknown ids typed and never "
           "invents a contract", "[harness][grounding-r4][probe][model]" )
{
    const ModelProbeOutcome empty = probeModelManifest( "" );
    CHECK_FALSE( empty.ok );
    CHECK( empty.code == error_codes::kInvalidParameter );

    const ModelProbeOutcome unknown = probeModelManifest( "r4/never-registered-model" );
    CHECK_FALSE( unknown.ok );
    CHECK( unknown.code == error_codes::kModelNotReady );
    CHECK( unknown.contract.empty() );
    // Stat-only fields stay honest on refusal.
    CHECK_FALSE( unknown.artifactPresent );
    CHECK( unknown.artifactBytes == -1 );
}

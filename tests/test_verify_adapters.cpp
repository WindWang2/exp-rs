// tests/test_verify_adapters.cpp
//
// ADR 0172 real provider adapters — oracle for the pure (Qt/GDAL-free)
// adapter layer: FsArtifactProbe, SidecarProvenanceView,
// CheckpointStateView, plus the two seams the whole track exists for:
//
//   1. real records (files on disk, not fakes) drive the REAL engine;
//   2. the SAME report projected into the harness shape, the lab-evidence
//      shape and the teaching feedback lens never flips a verdict — an
//      Indeterminate check fails every projection, it cannot pass anywhere.
//
// Locale discipline (canonicalJsonText contract): canonical text, digests
// and parse round-trips must be byte-identical under every numeric locale
// the host can switch to — the canonical text is the digest input, and a
// comma-decimal host must not be able to diverge from a dot-decimal one.
//
// Light target: sicnu_verify_adapters + sicnu_verifier + sicnu_teaching +
// Catch2 + jsoncpp; no Qt, no GDAL.

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <clocale>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "teaching/lab_feedback_projection.h"
#include "verify/projections.h"
#include "verify/verify_engine.h"
#include "verify/verify_error_codes.h"
#include "verify/verify_sha256.h"
#include "verify/verify_types.h"
#include "verify_adapters/bounded_io.h"
#include "verify_adapters/checkpoint_state_view.h"
#include "verify_adapters/fs_artifact_probe.h"
#include "verify_adapters/provenance_sidecar_view.h"

using namespace sicnu::verify;
namespace adapters = sicnu::verify_adapters;

namespace
{

/// Per-test fixture directory, removed on scope exit.
class TempDir
{
  public:
    TempDir()
    {
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        std::error_code ec;
        // Per-process unique tag: ctest runs each CASE as its own process,
        // and a deterministic name would make concurrent processes share
        // (and delete each other's) fixtures.
        const std::string tag = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count() );
        for ( int attempt = 0; attempt < 64 && mPath.empty(); ++attempt )
        {
            const std::filesystem::path candidate =
                base / ( "verify_adapters_" + tag + "_" + std::to_string( attempt ) );
            if ( std::filesystem::create_directories( candidate, ec ); !ec )
                mPath = candidate;
        }
        REQUIRE( !mPath.empty() );
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all( mPath, ec );
    }
    TempDir( const TempDir & ) = delete;
    TempDir &operator=( const TempDir & ) = delete;

    std::string file( const char *name ) const
    {
        return ( mPath / name ).generic_string();
    }
    std::string path() const { return mPath.generic_string(); }

    static void write( const std::string &path, const std::string &content )
    {
        std::ofstream out( path, std::ios::binary );
        REQUIRE( static_cast<bool>( out ) );
        out << content;
    }

  private:
    std::filesystem::path mPath;
};

/// The canonical exp-rs-prov/1 sidecar the model publish family writes.
std::string provSidecarJson()
{
    return R"( {
      "schema": "exp-rs-prov/1",
      "output": { "path": "ndvi.tif", "width": 96, "height": 96, "bands": 1, "format": "raster" },
      "model": { "identity_tag": "ndvi@2", "content_digest": "deadbeef" },
      "execution": { "backend": "cpu", "seed": 7 }
    } )";
}

/// The d17 provenance graph shape workflow_provenance writes.
std::string runProvenanceJson()
{
    return R"( {
      "kind": "d17_provenance",
      "version": "1.0",
      "nodes": [
        { "id": "run:r-1", "kind": "run",
          "attributes": { "workflowId": "wf-1", "planSignature": "sig-1", "schemaVersion": 1 } },
        { "id": "node:a", "kind": "nodeExec",
          "attributes": { "state": "succeeded", "lineageSignature": "sig-1" } },
        { "id": "artifact:ndvi.tif", "kind": "artifact",
          "attributes": { "fingerprint": "sha256full:aa", "size": 4096 } }
      ],
      "edges": [
        { "from": "node:a", "to": "artifact:ndvi.tif", "kind": "produced" }
      ]
    } )";
}

/// The checkpoint envelope pipeline_run_coordinator persists (v1.1 fields).
Json::Value checkpointDocument()
{
    Json::Value doc( Json::objectValue );
    doc["kind"] = "d17_pipeline_checkpoint";
    doc["version"] = "1.1";
    doc["runId"] = "r-1";
    doc["runDirectory"] = "/runs/r-1";
    doc["attempt"] = 1;
    doc["finished"] = false;
    doc["success"] = false;
    doc["updatedAt"] = "2026-09-25T00:00:00.000Z";
    Json::Value nodes( Json::arrayValue );
    const char *rows[][2] = { { "a", "Succeeded" }, { "b", "Failed" },    { "c", "Cancelled" },
                              { "d", "Pending" },   { "e", "Zombie" },   { "f", "Skipped" },
                              { "g", "Running" } };
    for ( const auto &row : rows )
    {
        Json::Value node( Json::objectValue );
        node["nodeId"] = row[0];
        node["state"] = row[1];
        node["progress"] = 0.5;
        node["artifact"] = std::string( "/runs/r-1/" ) + row[0] + ".tif";
        node["artifactFingerprint"] = "sha256fl:bb";
        nodes.append( node );
    }
    doc["nodes"] = nodes;
    return doc;
}

std::string serialize( const Json::Value &doc )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString( builder, doc );
}

} // namespace

TEST_CASE( "FsArtifactProbe projects real filesystem facts", "[verify_adapters]" )
{
    TempDir dir;
    const std::string raster = dir.file( "ndvi.tif" );
    TempDir::write( raster, "TIFF-bytes-here" );
    const std::string expectedDigest = sha256Hex( "TIFF-bytes-here" );

    adapters::FsArtifactProbe probe;

    SECTION( "existing artifact: exists, size, digest from the leaf sha256" )
    {
        const std::optional<ArtifactInfo> info = probe.probe( raster );
        REQUIRE( info.has_value() );
        REQUIRE( info->exists );
        CHECK( info->sizeBytes == 15 );
        CHECK( info->digest == expectedDigest );
        CHECK( info->kind == "raster" );
    }
    SECTION( "missing artifact: a definitive false, not a capability gap" )
    {
        const std::optional<ArtifactInfo> info = probe.probe( dir.file( "gone.tif" ) );
        REQUIRE( info.has_value() );
        CHECK_FALSE( info->exists );
    }
    SECTION( "digest budget: oversize artifacts report an unavailable digest" )
    {
        adapters::FsArtifactProbe smallProbe( 8 );
        const std::optional<ArtifactInfo> info = smallProbe.probe( raster );
        REQUIRE( info.has_value() );
        CHECK( info->digest.empty() ); // engine: verify:i_digest_unavailable
    }
    SECTION( "sidecar naming beats extension sniffing" )
    {
        const std::string sidecar = dir.file( "ndvi.tif.prov.json" );
        TempDir::write( sidecar, provSidecarJson() );
        const std::optional<ArtifactInfo> info = probe.probe( sidecar );
        REQUIRE( info.has_value() );
        CHECK( info->kind == "sidecar" );
    }
    SECTION( "hostile JSON: readJson refuses instead of mis-answering" )
    {
        TempDir::write( dir.file( "broken.json" ), "{ not json" );
        CHECK_FALSE( probe.readJson( dir.file( "broken.json" ) ).has_value() );
        CHECK_FALSE( probe.readJson( dir.file( "gone.json" ) ).has_value() );
    }
    SECTION( "a directory at an artifact path is not an artifact" )
    {
        // "exists" over a directory would let artifact.exists pass on
        // something that cannot be an artifact; the probe refuses instead.
        std::error_code ec;
        std::filesystem::create_directories(
            adapters::pathFromUtf8( dir.file( "mystery.tif" ) ), ec );
        REQUIRE( !ec );
        CHECK_FALSE( probe.probe( dir.file( "mystery.tif" ) ).has_value() );
    }
}

TEST_CASE( "SidecarProvenanceView projects only real, trusted records",
           "[verify_adapters][provenance]" )
{
    TempDir dir;
    const std::string product = dir.file( "ndvi.tif" );
    TempDir::write( product, "TIFF-bytes-here" );

    SECTION( "present sidecar: the engine judges the real document" )
    {
        TempDir::write( adapters::provenanceSidecarPathFor( product ), provSidecarJson() );
        adapters::SidecarProvenanceView view;
        const std::optional<Json::Value> doc = view.provenanceForPath( product );
        REQUIRE( doc.has_value() );
        CHECK( ( *doc )["schema"].asString() == "exp-rs-prov/1" );
        CHECK( ( *doc )["output"]["width"].asInt64() == 96 );
    }
    SECTION( "missing sidecar: typed Missing, engine sees no record (loud)" )
    {
        const adapters::ProvenanceReadResult read = adapters::readProvenanceSidecar( product );
        CHECK( read.status == adapters::ProvenanceReadStatus::Missing );
        adapters::SidecarProvenanceView view;
        CHECK_FALSE( view.provenanceForPath( product ).has_value() );
    }
    SECTION( "tampered/unreadable sidecar: Unreadable, never Ok" )
    {
        TempDir::write( adapters::provenanceSidecarPathFor( product ), "{ broken" );
        const adapters::ProvenanceReadResult read = adapters::readProvenanceSidecar( product );
        CHECK( read.status == adapters::ProvenanceReadStatus::Unreadable );
    }
    SECTION( "foreign schema: named, and never feeds the engine a completion" )
    {
        TempDir::write( adapters::provenanceSidecarPathFor( product ),
                        R"( { "schema": "exp-rs-prov/2", "output": {} } )" );
        const adapters::ProvenanceReadResult read = adapters::readProvenanceSidecar( product );
        CHECK( read.status == adapters::ProvenanceReadStatus::ForeignEnvelope );
        adapters::SidecarProvenanceView view;
        // A v2 sidecar is not the contracted record: projecting it would let
        // a completeness check pass against a document this build does not
        // trust. It reads as absence (a Fail), never as completion.
        CHECK_FALSE( view.provenanceForPath( product ).has_value() );
    }
    SECTION( "run lineage: d17_provenance envelope is gated" )
    {
        TempDir::write( dir.file( "provenance_r-1.json" ), runProvenanceJson() );
        adapters::SidecarProvenanceView view( { dir.path() } );
        const std::optional<Json::Value> doc = view.provenanceForRun( "r-1" );
        REQUIRE( doc.has_value() );
        CHECK( ( *doc )["nodes"].isArray() );

        TempDir::write( dir.file( "provenance_r-2.json" ),
                        R"( { "kind": "d17_provenance", "version": "9.9", "nodes": [] } )" );
        const adapters::ProvenanceReadResult future =
            adapters::readRunProvenance( "r-2", { dir.path() } );
        CHECK( future.status == adapters::ProvenanceReadStatus::ForeignEnvelope );
        CHECK_FALSE( view.provenanceForRun( "r-2" ).has_value() );
        CHECK( adapters::readRunProvenance( "r-none", { dir.path() } ).status ==
               adapters::ProvenanceReadStatus::Missing );
    }
}

TEST_CASE( "CheckpointStateView projects the recorded workflow vocabulary",
           "[verify_adapters][state]" )
{
    TempDir dir;
    const std::string checkpointPath = dir.file( "checkpoint_r-1.json" );
    TempDir::write( checkpointPath, serialize( checkpointDocument() ) );

    const adapters::CheckpointReadResult read = adapters::readCheckpoint( checkpointPath );
    REQUIRE( read.status == adapters::CheckpointReadStatus::Ok );
    adapters::CheckpointStateView view( read.document );

    SECTION( "recorded states project onto the closed vocabulary" )
    {
        CHECK( view.state( "node/a/state" )->asString() == "succeeded" );
        CHECK( view.state( "node/b/state" )->asString() == "failed" );
        CHECK( view.state( "node/c/state" )->asString() == "cancelled" );
        CHECK( view.state( "node/f/state" )->asString() == "skipped" );
        CHECK( view.state( "node/g/state" )->asString() == "running" );
        CHECK( view.state( "node/a/artifact" )->asString() == "/runs/r-1/a.tif" );
        CHECK( view.state( "node/a/fingerprint" )->asString() == "sha256fl:bb" );
        CHECK( view.state( "run/id" )->asString() == "r-1" );
        CHECK( view.state( "run/finished" )->asBool() == false );
    }
    SECTION( "an unknown state spelling can never read as success" )
    {
        REQUIRE( view.state( "node/e/state" )->asString() == "unknown" );
        CHECK_FALSE( view.state( "node/z/state" ).has_value() ); // no such node
    }
    SECTION( "node ids containing '/' split on the last separator" )
    {
        Json::Value doc = checkpointDocument();
        Json::Value nested( Json::objectValue );
        nested["nodeId"] = "group/sub/task";
        nested["state"] = "Succeeded";
        nested["artifact"] = "/runs/r-1/task.tif";
        doc["nodes"].append( nested );
        adapters::CheckpointStateView nestedView( doc );
        CHECK( nestedView.state( "node/group/sub/task/state" )->asString() == "succeeded" );
        CHECK( nestedView.state( "node/group/sub/task/artifact" )->asString() ==
               "/runs/r-1/task.tif" );
        // A shorter key names a DIFFERENT (nonexistent) node — never the
        // nested one's state.
        CHECK_FALSE( nestedView.state( "node/group/sub/state" ).has_value() );
        CHECK( nestedView.state( "node/group/sub/task/flavour" ).has_value() == false );
    }
    SECTION( "foreign envelope: typed, and the view answers nothing" )
    {
        Json::Value future = checkpointDocument();
        future["version"] = "9.9";
        const adapters::CheckpointReadResult futureRead =
            adapters::readCheckpoint( dir.file( "checkpoint_missing.json" ) );
        CHECK( futureRead.status == adapters::CheckpointReadStatus::Missing );
        // Even a direct construction from a WELL-FORMED future-version
        // document stays unusable: the closed version set is the gate.
        adapters::CheckpointStateView foreignView( future );
        CHECK_FALSE( foreignView.state( "node/a/state" ).has_value() );
        TempDir::write( dir.file( "checkpoint_r-9.json" ), serialize( future ) );
        const adapters::CheckpointReadResult gated =
            adapters::readCheckpoint( dir.file( "checkpoint_r-9.json" ) );
        CHECK( gated.status == adapters::CheckpointReadStatus::ForeignEnvelope );
    }
    SECTION( "hostile documents: the view answers nullopt, never throws" )
    {
        // Non-object documents must load as unusable — jsoncpp's non-const
        // operator[] would throw here if objectness were not settled first.
        adapters::CheckpointStateView arrayView( Json::Value( Json::arrayValue ) );
        CHECK_FALSE( arrayView.state( "node/a/state" ).has_value() );
        adapters::CheckpointStateView stringView( Json::Value( "d17_pipeline_checkpoint" ) );
        CHECK_FALSE( stringView.state( "run/id" ).has_value() );
        adapters::CheckpointStateView nullView( Json::Value{ Json::nullValue } );
        CHECK_FALSE( nullView.state( "node/a/state" ).has_value() );
    }
    SECTION( "oversized checkpoint is capped, not buffered" )
    {
        const std::string giant( adapters::kMaxAdapterDocumentBytes + 1, ' ' );
        TempDir::write( dir.file( "checkpoint_big.json" ), giant );
        CHECK( adapters::readCheckpoint( dir.file( "checkpoint_big.json" ) ).status ==
               adapters::CheckpointReadStatus::Oversized );
    }
}

TEST_CASE( "Real adapters drive the real engine end to end", "[verify_adapters][engine]" )
{
    TempDir dir;
    const std::string product = dir.file( "ndvi.tif" );
    TempDir::write( product, "TIFF-bytes-here" );
    TempDir::write( adapters::provenanceSidecarPathFor( product ), provSidecarJson() );
    const std::string checkpointPath = dir.file( "checkpoint_r-1.json" );
    TempDir::write( checkpointPath, serialize( checkpointDocument() ) );

    adapters::FsArtifactProbe artifactProbe;
    adapters::SidecarProvenanceView provenanceView( { dir.path() } );
    const adapters::CheckpointReadResult read = adapters::readCheckpoint( checkpointPath );
    REQUIRE( read.status == adapters::CheckpointReadStatus::Ok );
    adapters::CheckpointStateView stateView( read.document );

    VerificationSpec spec;
    spec.specId = "spec.adapter.e2e";
    spec.scope = "node";

    VerificationCheckSpec exists;
    exists.checkId = "product-exists";
    exists.kind = "artifact.exists";
    exists.params["path"] = product;
    spec.checks.push_back( exists );

    VerificationCheckSpec schema;
    schema.checkId = "sidecar-schema";
    schema.kind = "artifact.schema";
    schema.params["path"] = adapters::provenanceSidecarPathFor( product );
    schema.params["schemaId"] = "exp-rs-prov/1";
    spec.checks.push_back( schema );

    VerificationCheckSpec provenance;
    provenance.checkId = "product-provenance";
    provenance.kind = "provenance.complete";
    provenance.params["path"] = product;
    Json::Value requiredFields( Json::arrayValue );
    for ( const char *field : { "schema", "output", "model", "execution" } )
        requiredFields.append( field );
    provenance.params["requiredFields"] = requiredFields;
    spec.checks.push_back( provenance );

    VerificationCheckSpec state;
    state.checkId = "node-a-succeeded";
    state.kind = "state.invariant";
    Json::Value expectations( Json::arrayValue );
    Json::Value expectation( Json::objectValue );
    expectation["key"] = "node/a/state";
    expectation["op"] = "eq";
    expectation["value"] = "succeeded";
    expectations.append( expectation );
    state.params["expectations"] = expectations;
    spec.checks.push_back( state );

    REQUIRE( validateSpec( spec ).empty() );

    const VerificationContext context{ &artifactProbe, nullptr, &stateView, &provenanceView,
                                       nullptr };
    const VerificationReport report = evaluate( spec, context );
    CHECK( report.overall == VerificationStatus::Pass );
    CHECK( report.passCount() == 4 );

    SECTION( "content edits inside the envelope are the spec's business" )
    {
        // The view projects the record as written; it does not silently
        // re-judge content. A recorded width change survives the schema and
        // completeness checks — callers pin such facts with a digest check
        // (reproducibility.digest), which is the engine vocabulary for it.
        std::string editedSidecar = provSidecarJson();
        const auto widthAt = editedSidecar.find( "\"width\": 96" );
        REQUIRE( widthAt != std::string::npos );
        editedSidecar.replace( widthAt, 11, "\"width\": 128" );
        TempDir::write( adapters::provenanceSidecarPathFor( product ), editedSidecar );
        const VerificationReport edited = evaluate( spec, context );
        CHECK( edited.checks[1].status == VerificationStatus::Pass ); // schema still exp-rs-prov/1
        CHECK( edited.checks[2].status == VerificationStatus::Pass ); // still complete
        CHECK( edited.overall == VerificationStatus::Pass );
    }

    SECTION( "missing sidecar: provenance completion fails, never skips" )
    {
        std::error_code ec;
        std::filesystem::remove( adapters::provenanceSidecarPathFor( product ), ec );
        const VerificationReport missing = evaluate( spec, context );
        CHECK( missing.checks[2].status == VerificationStatus::Fail );
        CHECK( missing.checks[2].code == std::string( kCodeProvenanceIncomplete ) );
        CHECK( missing.overall == VerificationStatus::Fail );
    }

    SECTION( "unknown workflow state cannot satisfy a success pin" )
    {
        Json::Value doc = checkpointDocument();
        doc["nodes"][0]["state"] = "Zombie";
        TempDir::write( checkpointPath, serialize( doc ) );
        const adapters::CheckpointReadResult reloaded = adapters::readCheckpoint( checkpointPath );
        adapters::CheckpointStateView reloadedView( reloaded.document );
        const VerificationContext hostileContext{ &artifactProbe, nullptr, &reloadedView,
                                                  &provenanceView, nullptr };
        const VerificationReport hostile = evaluate( spec, hostileContext );
        CHECK( hostile.checks[3].status == VerificationStatus::Fail );
        CHECK( hostile.overall == VerificationStatus::Fail );
    }
}

TEST_CASE( "One report, three projections, zero verdict flips",
           "[verify_adapters][projections][teaching]" )
{
    // A report whose second check is Indeterminate (the adapter could not
    // answer) — the exact document every fail-closed surface must refuse to
    // call a success.
    VerificationCheckResult pass;
    pass.checkId = "c1";
    pass.kind = "artifact.exists";
    pass.status = VerificationStatus::Pass;
    VerificationCheckResult indeterminate;
    indeterminate.checkId = "c2";
    indeterminate.kind = "artifact.grid";
    indeterminate.status = VerificationStatus::Indeterminate;
    indeterminate.code = kCodeArtifactUnreadable;
    indeterminate.message = "grid probe cannot answer";
    const VerificationReport report = buildReport( "spec.x", "node", "digest-x",
                                                   { pass, indeterminate } );

    // Lens 1 — harness shape: not a PASS, and the unknown check is
    // error-severity so re-aggregation stays fail-closed.
    const Json::Value harness = harnessVerificationFromReport( report );
    CHECK( harness["verdict"].asString() == "FAIL" );
    CHECK( harness["checks"][1]["severity"].asString() == "error" );

    // Lens 2 — teaching lab evidence: the indeterminate check is not passed.
    const Json::Value lab = labEvidenceFromReport( report );
    CHECK( lab["evidence"][1]["passed"].asBool() == false );

    // Lens 3 — the teaching feedback projection consuming the unified
    // report document: indeterminate cannot count as pass, overall is not
    // pass, and the projection names the real check id and message.
    Json::Value reportDoc = report.toCanonicalJson();
    const sicnu::teaching::LabFeedbackProjection projection =
        sicnu::teaching::LabFeedbackProjection::fromReports( "lab-1", reportDoc, Json::Value() );
    CHECK( projection.ok );
    CHECK( projection.overallStatus != "pass" );
    CHECK_FALSE( projection.overallCountsAsPass );
    bool sawIndeterminate = false;
    for ( const auto &row : projection.rows )
    {
        if ( row.status != "pass" )
            CHECK_FALSE( row.countsAsPass );
        if ( row.id == "c2" )
        {
            sawIndeterminate = true;
            CHECK( row.status == "indeterminate" );
            CHECK( row.reasonZh == "grid probe cannot answer" );
        }
    }
    CHECK( sawIndeterminate );
}

TEST_CASE( "Canonical text and digests are numeric-locale invariants",
           "[verify_adapters][locale]" )
{
    // The digest seal is only portable if LC_NUMERIC cannot change the
    // canonical text. Sweep every locale the host actually offers (this
    // covers C/POSIX everywhere and comma-decimal locales where installed).
    struct LocaleRestorer
    {
        std::string saved = setlocale( LC_NUMERIC, nullptr );
        ~LocaleRestorer() { setlocale( LC_NUMERIC, saved.c_str() ); }
    } restorer;

    const char *locales[] = { "C",    "C.utf8",  "POSIX",       "c",            "posix",
                              "en_US.utf8", "en_US.UTF-8", "zh_CN.utf8", "zh_CN.UTF-8",
                              "de_DE.utf8", "de_DE.UTF-8" };

    Json::Value body( Json::objectValue );
    body["sum"] = 0.1 + 0.2;
    body["tiny"] = 1e-13;
    body["big"] = 123456789.123456789;
    body["neg"] = -0.25;
    body["whole"] = 4.0;

    std::string referenceText;
    std::string referenceDigest;
    bool sweptAny = false;
    for ( const char *locale : locales )
    {
        if ( !setlocale( LC_NUMERIC, locale ) )
            continue; // not installed on this host
        const std::string text = canonicalJsonText( body );
        REQUIRE_FALSE( text.empty() );
        // The decimal POINT must sit in the number itself: on a host whose
        // numeric locale formats "0,3", this is where the divergence shows.
        const std::size_t sumAt = text.find( "\"sum\":" );
        REQUIRE( sumAt != std::string::npos );
        CHECK( text.compare( sumAt + 6, 3, "0.3" ) == 0 );
        if ( !sweptAny )
        {
            referenceText = text;
            referenceDigest = sha256Hex( text );
            sweptAny = true;
        }
        else
        {
            INFO( "locale " << locale );
            CHECK( text == referenceText );
            CHECK( sha256Hex( text ) == referenceDigest );
        }
        // Round-trip: the canonical text parses back to the same values
        // under this locale too.
        std::string error;
        const std::optional<Json::Value> back = sicnu::verify_adapters::parseJsonBounded( text, error );
        REQUIRE( back.has_value() );
        CHECK( back->size() == body.size() );
    }
    CHECK( sweptAny );

    // The spec/report digest path routes through the same writer: pin the
    // spec digest under one locale and re-derive under another.
    if ( setlocale( LC_NUMERIC, "C" ) )
    {
        VerificationSpec spec;
        spec.specId = "spec.locale";
        spec.scope = "node";
        VerificationCheckSpec check;
        check.checkId = "grid";
        check.kind = "artifact.grid";
        check.params["path"] = "/data/x.tif";
        check.params["width"] = 96;
        spec.checks.push_back( check );
        const std::string digestC = specDigest( spec );
        if ( setlocale( LC_NUMERIC, "POSIX" ) )
            CHECK( specDigest( spec ) == digestC );
        if ( setlocale( LC_NUMERIC, "en_US.utf8" ) )
            CHECK( specDigest( spec ) == digestC );
    }
}

// ---------------------------------------------------------------------------
// Track 16 WP-A: adapter coverage matrix closures. ADAPTER_MATRIX.md rows:
// checkpoint Unreadable typed cell, bounded_io direct negative cells, and
// the widest production-adapter assembly cell (four production adapters
// plus the metric seam — which has no production adapter; the fake here is
// the declared gap, see ADAPTER_MATRIX.md).
// ---------------------------------------------------------------------------

TEST_CASE( "an unreadable checkpoint file is a typed Unreadable, not a guess",
           "[verify_adapters][track16][checkpoint]" )
{
    TempDir dir;
    const std::string bad = dir.file( "checkpoint_bad.json" );
    TempDir::write( bad, "{ this is not json at all" );
    const adapters::CheckpointReadResult result = adapters::readCheckpoint( bad );
    CHECK( result.status == adapters::CheckpointReadStatus::Unreadable );
    CHECK_FALSE( result.detail.empty() );

    // Contrast cell: the same path with a valid document reads Ok.
    const std::string good = dir.file( "checkpoint_good.json" );
    TempDir::write( good, serialize( checkpointDocument() ) );
    CHECK( adapters::readCheckpoint( good ).status == adapters::CheckpointReadStatus::Ok );
}

TEST_CASE( "bounded_io refuses oversized, missing and malformed inputs typed",
           "[verify_adapters][track16][bounded_io]" )
{
    TempDir dir;

    // Over the cap: typed OverCap, never an unbounded buffer.
    const std::string big = dir.file( "big.bin" );
    TempDir::write( big, std::string( 4096, 'x' ) );
    const adapters::IoResult over =
        adapters::readFileBounded( big, 1024 );
    CHECK( over.failure == adapters::IoFailure::OverCap );

    // Missing path: typed Missing.
    const adapters::IoResult missing =
        adapters::readFileBounded( dir.file( "absent.bin" ), 1024 );
    CHECK( missing.failure == adapters::IoFailure::Missing );

    // Malformed JSON: typed nullopt with an error string, never a throw.
    std::string error;
    CHECK_FALSE( adapters::parseJsonBounded( "{ not json", error ).has_value() );
    CHECK_FALSE( error.empty() );
    const std::optional<Json::Value> ok = adapters::parseJsonBounded( "{\"a\":1}", error );
    REQUIRE( ok.has_value() );
    CHECK( ( *ok )["a"].asInt() == 1 );

    // Existence goes through the same UTF-8 decoding contract.
    CHECK( adapters::pathExists( big ) );
    CHECK_FALSE( adapters::pathExists( dir.file( "absent.bin" ) ) );
}

TEST_CASE( "the widest production adapter assembly drives five check kinds "
           "through one evaluation",
           "[verify_adapters][track16][matrix]" )
{
    TempDir dir;
    const std::string content = "track16-repro-bytes";
    const std::string product = dir.file( "output.tif" );
    TempDir::write( product, content );
    TempDir::write( adapters::provenanceSidecarPathFor( product ), provSidecarJson() );
    const std::string checkpointPath = dir.file( "checkpoint_r-1.json" );
    TempDir::write( checkpointPath, serialize( checkpointDocument() ) );

    adapters::FsArtifactProbe artifactProbe;
    adapters::SidecarProvenanceView provenanceView( { dir.path() } );
    const adapters::CheckpointReadResult read = adapters::readCheckpoint( checkpointPath );
    REQUIRE( read.status == adapters::CheckpointReadStatus::Ok );
    adapters::CheckpointStateView stateView( read.document );

    // The metric seam has NO production adapter yet (declared gap in
    // ADAPTER_MATRIX.md); this local fake keeps the assembly row honest
    // about the metric dimension without inventing production code.
    class FakeMetric final : public IMetricView
    {
      public:
        std::optional<double> metric( const std::string &name ) override
        {
            if ( name == "ndvi_mean" )
                return 0.42;
            return std::nullopt;
        }
    } metricView;

    VerificationSpec spec;
    spec.specId = "spec.adapter.wide";
    spec.scope = "node";

    VerificationCheckSpec exists;
    exists.checkId = "product-exists";
    exists.kind = "artifact.exists";
    exists.params["path"] = product;
    spec.checks.push_back( exists );

    VerificationCheckSpec provenance;
    provenance.checkId = "product-provenance";
    provenance.kind = "provenance.complete";
    provenance.params["path"] = product;
    Json::Value requiredFields( Json::arrayValue );
    for ( const char *field : { "schema", "output", "model", "execution" } )
        requiredFields.append( field );
    provenance.params["requiredFields"] = requiredFields;
    spec.checks.push_back( provenance );

    VerificationCheckSpec state;
    state.checkId = "node-a-succeeded";
    state.kind = "state.invariant";
    Json::Value expectations( Json::arrayValue );
    Json::Value expectation( Json::objectValue );
    expectation["key"] = "node/a/state";
    expectation["op"] = "eq";
    expectation["value"] = "succeeded";
    expectations.append( expectation );
    state.params["expectations"] = expectations;
    spec.checks.push_back( state );

    VerificationCheckSpec metric;
    metric.checkId = "ndvi-in-band";
    metric.kind = "metric.range";
    metric.params["metric"] = "ndvi_mean";
    metric.params["min"] = 0.3;
    metric.params["max"] = 0.5;
    spec.checks.push_back( metric );

    VerificationCheckSpec reproducible;
    reproducible.checkId = "bytes-match";
    reproducible.kind = "reproducibility.digest";
    reproducible.params["path"] = product;
    reproducible.params["expectedDigest"] = sha256Hex( content );
    spec.checks.push_back( reproducible );

    REQUIRE( validateSpec( spec ).empty() );

    VerificationContext context;
    context.artifactProbe = &artifactProbe;
    context.provenanceView = &provenanceView;
    context.stateView = &stateView;
    context.metricView = &metricView;
    const VerificationReport report = evaluate( spec, context );

    REQUIRE( report.checks.size() == 5 );
    CHECK( report.checks[0].status == VerificationStatus::Pass ); // exists
    CHECK( report.checks[1].status == VerificationStatus::Pass ); // provenance
    CHECK( report.checks[2].status == VerificationStatus::Pass ); // state
    CHECK( report.checks[3].status == VerificationStatus::Pass ); // metric
    CHECK( report.checks[4].status == VerificationStatus::Pass ); // reproducibility
    CHECK( report.overall == VerificationStatus::Pass );
    CHECK_FALSE( report.digest().empty() );
}

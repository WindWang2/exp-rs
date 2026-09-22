// test_grader_schema.cpp — RS14-05 Slice A: versioned schema contracts of the
// process-aware experiment grader (ADR 0174). Light lane: Catch2 + sicnu_grader
// only. Covers: SHA-256 (NIST vectors), canonical JSON (sorted members,
// shortest round-trip numbers, non-finite refusal), strict parsing, closed
// enum vocabularies, rubric/evidence/report serde + validation (fail closed,
// typed paths), report digest binding, engine refusal of invalid documents,
// and hostile-shape typed refusals.
//
// RED-first record: at stub stage every serde/canonical/digest case failed
// (stubs return typed Internal errors / empty spellings for the parsers).
#include "grader/grader_engine.h"
#include "grader/grader_error.h"
#include "grader/grader_json.h"
#include "grader/grader_sha256.h"
#include "grader/grader_types.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using namespace sicnu::grader;

namespace {

GraderError noopError()
{
    return {};
}

Json::Value rubricJson( const std::string &schema = "sicnu.grader.rubric/1" )
{
    Json::Value doc{ Json::objectValue };
    doc["schema"] = schema;
    doc["rubricId"] = "lab03-supervised";
    doc["revision"] = 1;
    doc["title"] = "Lab 03";
    doc["totalPoints"] = 100.0;
    doc["passingScore"] = 60.0;
    Json::Value criterion{ Json::objectValue };
    criterion["criterionId"] = "proc-train";
    criterion["title"] = "Training stage completed";
    criterion["maxPoints"] = 100.0;
    criterion["kind"] = "stage";
    criterion["evidenceKey"] = "train";
    criterion["stage"] = Json::Value{ Json::objectValue };
    criterion["stage"]["expectedState"] = "Completed";
    Json::Value dim{ Json::objectValue };
    dim["dimensionId"] = "process";
    dim["title"] = "Process";
    dim["weight"] = 100.0;
    dim["criteria"] = Json::Value{ Json::arrayValue };
    dim["criteria"].append( criterion );
    doc["dimensions"] = Json::Value{ Json::arrayValue };
    doc["dimensions"].append( dim );
    return doc;
}

Json::Value evidenceJson( const std::string &schema = "sicnu.grader.evidence/1" )
{
    Json::Value doc{ Json::objectValue };
    doc["schema"] = schema;
    Json::Value item{ Json::objectValue };
    item["evidenceId"] = "ev-1";
    item["kind"] = "stage";
    item["key"] = "train";
    item["state"] = "Completed";
    item["source"] = "provenance:prov.json";
    doc["items"] = Json::Value{ Json::arrayValue };
    doc["items"].append( item );
    return doc;
}

} // namespace

TEST_CASE( "sha256 NIST vectors", "[grader][schema][sha]" )
{
    CHECK( sha256Hex( "" ) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
    CHECK( sha256Hex( "abc" ) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
    CHECK( sha256Hex( "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" ) ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" );
    // One million 'a' would be slow here; the streaming/one-shot identity
    // plus multi-block vectors above are enough for this lane.
    Sha256 stream;
    stream.update( "abcdbcde", 8 );
    stream.update( "cdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 48 );
    CHECK( stream.hexDigest() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" );
}

TEST_CASE( "canonicalNumber is shortest round-trip and refuses non-finite", "[grader][schema][json]" )
{
    auto err = noopError();
    CHECK( canonicalNumber( 0.0, err ) == "0" );
    CHECK( canonicalNumber( -0.0, err ) == "0" );
    CHECK( canonicalNumber( 1.0, err ) == "1" );
    CHECK( canonicalNumber( -42.0, err ) == "-42" );
    CHECK( canonicalNumber( 0.5, err ) == "0.5" );
    CHECK( canonicalNumber( 0.1, err ) == "0.1" );
    CHECK( canonicalNumber( 100.0, err ) == "100" );
    // Round-trip identity for a value with no short spelling.
    const std::string ugly = canonicalNumber( 0.8571428571428571, err ).value();
    double back = 0.0;
    sscanf( ugly.c_str(), "%lf", &back );
    CHECK( back == 0.8571428571428571 );
    // Non-finite numbers must be refused, never serialized.
    err = {};
    CHECK_FALSE( canonicalNumber( std::nan( "" ), err ).has_value() );
    CHECK( err.code == GraderErrorCode::Internal );
    err = {};
    CHECK_FALSE( canonicalNumber( HUGE_VAL, err ).has_value() );
}

TEST_CASE( "canonicalizeJson sorts members and is byte-stable", "[grader][schema][json]" )
{
    auto err = noopError();
    Json::Value doc{ Json::objectValue };
    doc["zebra"] = 1;
    doc["alpha"] = Json::Value{ Json::objectValue };
    doc["alpha"]["y"] = 2.5;
    doc["alpha"]["x"] = "hi";
    doc["middle"] = Json::Value{ Json::arrayValue };
    doc["middle"].append( 3 );
    doc["middle"].append( "b" );

    const std::string first = canonicalizeJson( doc, err ).value();
    const std::string second = canonicalizeJson( doc, err ).value();
    REQUIRE( first == second );
    // Members sorted by key, arrays keep order, compact separators.
    CHECK( first == R"({"alpha":{"x":"hi","y":2.5},"middle":[3,"b"],"zebra":1})" );

    // Non-finite payload anywhere → refusal with typed error.
    Json::Value bad{ Json::objectValue };
    bad["ok"] = 1;
    bad["bad"] = std::nan( "" );
    err = {};
    CHECK_FALSE( canonicalizeJson( bad, err ).has_value() );
    CHECK( err.code == GraderErrorCode::Internal );
    CHECK( err.path == "bad" );
}

TEST_CASE( "parseJsonStrict refuses trailing garbage and comments", "[grader][schema][json]" )
{
    auto err = noopError();
    Json::Value doc;
    REQUIRE( parseJsonStrict( R"({"a":1})", err ).has_value() );
    CHECK( ( *parseJsonStrict( R"({"a":1})", err ) )["a"].asInt() == 1 );

    err = {};
    CHECK_FALSE( parseJsonStrict( R"({"a":1} tail)", err ).has_value() );
    CHECK( err.code == GraderErrorCode::InvalidJson );

    err = {};
    CHECK_FALSE( parseJsonStrict( "{ /* no comments */ }", err ).has_value() );
    CHECK( err.code == GraderErrorCode::InvalidJson );

    err = {};
    CHECK_FALSE( parseJsonStrict( "not json", err ).has_value() );
}

TEST_CASE( "enum vocabularies are closed and round-trip", "[grader][schema][vocab]" )
{
    // Criterion kinds.
    for ( const char *spelling : { "stage", "metric", "fact", "answer" } ) {
        auto kind = parseCriterionKind( spelling );
        REQUIRE( kind.has_value() );
        CHECK( criterionKindSpelling( *kind ) == spelling );
    }
    CHECK_FALSE( parseCriterionKind( "llm_vibe" ).has_value() );
    CHECK_FALSE( parseCriterionKind( "" ).has_value() );

    // Evidence kinds.
    for ( const char *spelling :
          { "stage", "metric", "artifact_state", "provenance", "checkpoint", "answer", "artifact_grade",
            "verifier_verdict", "replay_readiness", "custom" } ) {
        auto kind = parseEvidenceKind( spelling );
        REQUIRE( kind.has_value() );
        CHECK( evidenceKindSpelling( *kind ) == spelling );
    }
    CHECK_FALSE( parseEvidenceKind( "button_click" ).has_value() );
    CHECK_FALSE( parseEvidenceKind( "Stage" ).has_value() ); // case-sensitive

    // Outcome statuses / verdicts.
    CHECK( outcomeStatusSpelling( OutcomeStatus::Earned ) == "earned" );
    CHECK( outcomeStatusSpelling( OutcomeStatus::Partial ) == "partial" );
    CHECK( outcomeStatusSpelling( OutcomeStatus::NotEarned ) == "not_earned" );
    CHECK( outcomeStatusSpelling( OutcomeStatus::Indeterminate ) == "indeterminate" );
    CHECK( outcomeStatusSpelling( OutcomeStatus::Capped ) == "capped" );
    CHECK( reportVerdictSpelling( ReportVerdict::Pass ) == "pass" );
    CHECK( reportVerdictSpelling( ReportVerdict::Partial ) == "partial" );
    CHECK( reportVerdictSpelling( ReportVerdict::Fail ) == "fail" );
    CHECK( reportVerdictSpelling( ReportVerdict::Blocked ) == "blocked" );
}

TEST_CASE( "rubric serde round-trips", "[grader][schema][rubric]" )
{
    auto err = noopError();
    Json::Value doc = rubricJson();
    auto rubric = GradingRubric::fromJson( doc, err );
    REQUIRE( rubric.has_value() );
    CHECK( rubric->rubricId == "lab03-supervised" );
    CHECK( rubric->revision == 1 );
    REQUIRE( rubric->dimensions.size() == 1 );
    REQUIRE( rubric->dimensions[0].criteria.size() == 1 );
    CHECK( rubric->dimensions[0].criteria[0].criterionId == "proc-train" );
    CHECK( rubric->dimensions[0].criteria[0].kind == CriterionKind::Stage );
    CHECK( rubric->dimensions[0].criteria[0].stage.expectedState == "Completed" );
    CHECK( rubric->validate( err ) );

    // Back through toJson → fromJson keeps the same ids and structure.
    Json::Value doc2 = rubric->toJson();
    auto rubric2 = GradingRubric::fromJson( doc2, err );
    REQUIRE( rubric2.has_value() );
    CHECK( rubric2->dimensions[0].criteria[0].criterionId == "proc-train" );

    // Canonical bytes are insertion-order independent.
    auto errA = noopError();
    auto errB = noopError();
    const std::string canonA = canonicalizeJson( rubricJson(), errA ).value();
    Json::Value reordered = rubricJson();
    Json::Value tmp = reordered;
    reordered = Json::Value{ Json::objectValue };
    for ( const char *key : { "dimensions", "passingScore", "totalPoints", "title", "revision", "rubricId", "schema" } )
        reordered[key] = tmp[key];
    const std::string canonB = canonicalizeJson( reordered, errB ).value();
    CHECK( canonA == canonB );
}

TEST_CASE( "rubric serde refuses foreign/missing schema versions", "[grader][schema][rubric]" )
{
    auto err = noopError();
    GradingRubric::fromJson( rubricJson( "sicnu.grader.rubric/2" ), err );
    CHECK( err.code == GraderErrorCode::SchemaVersionUnsupported );
    CHECK( toCodeString( err.code ) == "grader:e-schema-version" );

    err = {};
    Json::Value doc = rubricJson();
    doc.removeMember( "schema" );
    GradingRubric::fromJson( doc, err );
    CHECK( err.code == GraderErrorCode::SchemaShapeInvalid );
}

TEST_CASE( "rubric validation fails closed with typed paths", "[grader][schema][rubric]" )
{
    auto err = noopError();

    SECTION( "weight sum mismatch" )
    {
        Json::Value doc = rubricJson();
        doc["dimensions"][0]["weight"] = 90.0;
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.code == GraderErrorCode::SchemaShapeInvalid );
        CHECK( err.path.find( "weight" ) != std::string::npos );
    }

    SECTION( "dimension weights must sum to totalPoints" )
    {
        Json::Value doc = rubricJson();
        doc["totalPoints"] = 99.0;
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.message.find( "totalPoints" ) != std::string::npos );
    }

    SECTION( "duplicate criterionId across dimensions" )
    {
        Json::Value doc = rubricJson();
        Json::Value criterion = doc["dimensions"][0]["criteria"][0];
        Json::Value dim{ Json::objectValue };
        dim["dimensionId"] = "result";
        dim["title"] = "Result";
        dim["weight"] = 0.0; // will fail weight first — give it a zero-point criterion instead
        Json::Value c2 = criterion;
        c2["maxPoints"] = 0.0; // invalid: maxPoints must be > 0; use distinct id to isolate dup check
        c2 = criterion;
        c2["maxPoints"] = doc["dimensions"][0]["criteria"][0]["maxPoints"].asDouble(); // keeps sum broken? no
        // Simplest isolation: second dimension with one more 0-weight not allowed —
        // instead duplicate the id within the same dimension and rebalance is
        // impossible with one criterion; so assert duplicate detection happens
        // before weight-sum by using two criteria in one dimension.
        doc["dimensions"][0]["criteria"].append( criterion ); // same id, total weight 200
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.message.find( "proc-train" ) != std::string::npos );
    }

    SECTION( "unknown criterion kind spelling" )
    {
        Json::Value doc = rubricJson();
        doc["dimensions"][0]["criteria"][0]["kind"] = "llm_vibe";
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.code == GraderErrorCode::SchemaShapeInvalid );
        CHECK( err.path.find( "kind" ) != std::string::npos );
    }

    SECTION( "metric range needs valueMax" )
    {
        Json::Value doc = rubricJson();
        Json::Value criterion{ Json::objectValue };
        criterion["criterionId"] = "res-acc";
        criterion["title"] = "Accuracy";
        criterion["maxPoints"] = 100.0;
        criterion["kind"] = "metric";
        criterion["evidenceKey"] = "overall_accuracy";
        criterion["metric"] = Json::Value{ Json::objectValue };
        criterion["metric"]["mode"] = "range";
        criterion["metric"]["value"] = 0.8;
        doc["dimensions"][0]["criteria"][0] = criterion;
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.code == GraderErrorCode::SchemaShapeInvalid );
    }

    SECTION( "negative tolerance refused" )
    {
        Json::Value doc = rubricJson();
        Json::Value criterion = doc["dimensions"][0]["criteria"][0];
        criterion["criterionId"] = "res-acc";
        criterion["kind"] = "metric";
        criterion["metric"] = Json::Value{ Json::objectValue };
        criterion["metric"]["mode"] = "at_least";
        criterion["metric"]["value"] = 0.85;
        criterion["metric"]["tolerance"] = -0.01;
        doc["dimensions"][0]["criteria"][0] = criterion;
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
    }

    SECTION( "hard constraint cap must be below totalPoints" )
    {
        Json::Value doc = rubricJson();
        Json::Value hc{ Json::objectValue };
        hc["constraintId"] = "hc-1";
        hc["title"] = "No leakage";
        hc["mode"] = "forbidden";
        hc["evidenceKey"] = "leaky_step";
        hc["effect"] = "cap";
        hc["capPoints"] = 100.0; // binds nothing → rubric bug
        doc["hardConstraints"] = Json::Value{ Json::arrayValue };
        doc["hardConstraints"].append( hc );
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.path.find( "capPoints" ) != std::string::npos );
    }

    SECTION( "orderedAfter must reference a real stage key" )
    {
        Json::Value doc = rubricJson();
        doc["dimensions"][0]["criteria"][0]["stage"]["orderedAfter"] = Json::Value{ Json::arrayValue };
        doc["dimensions"][0]["criteria"][0]["stage"]["orderedAfter"].append( "ghost_stage" );
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.path.find( "orderedAfter" ) != std::string::npos );
    }

    SECTION( "pathway must reference real criteria" )
    {
        Json::Value doc = rubricJson();
        Json::Value pathway{ Json::objectValue };
        pathway["pathwayId"] = "p1";
        pathway["criterionIds"] = Json::Value{ Json::arrayValue };
        pathway["criterionIds"].append( "nope" );
        doc["alternatePathways"] = Json::Value{ Json::arrayValue };
        doc["alternatePathways"].append( pathway );
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.message.find( "nope" ) != std::string::npos );
        CHECK( err.path.find( "criterionIds" ) != std::string::npos );
    }

    SECTION( "answer criterion needs concepts or misconceptions" )
    {
        Json::Value doc = rubricJson();
        Json::Value criterion = doc["dimensions"][0]["criteria"][0];
        criterion["criterionId"] = "interp-q1";
        criterion["kind"] = "answer";
        criterion["answer"] = Json::Value{ Json::objectValue };
        criterion["answer"]["questionId"] = "q1";
        doc["dimensions"][0]["criteria"][0] = criterion;
        err = {};
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.path.find( "answer" ) != std::string::npos );
    }

    SECTION( "criteria budget enforced" )
    {
        auto err2 = noopError();
        GradingRubric rubric;
        rubric.rubricId = "bulk";
        rubric.revision = 1;
        rubric.title = "bulk";
        rubric.totalPoints = 1.0;
        rubric.budgets.maxCriteria = 3;
        Dimension dim;
        dim.dimensionId = "d";
        dim.title = "d";
        dim.weight = 1.0;
        for ( int i = 0; i < 4; ++i ) {
            Criterion c;
            c.criterionId = "c" + std::to_string( i );
            c.title = "c";
            c.maxPoints = 0.25;
            c.kind = CriterionKind::Stage;
            c.evidenceKey = "k" + std::to_string( i );
            c.stage.expectedState = "Completed";
            dim.criteria.push_back( c );
        }
        rubric.dimensions.push_back( dim );
        CHECK_FALSE( rubric.validate( err2 ) );
        CHECK( toCodeString( err2.code ) == "grader:e-schema-shape" );
        CHECK( err2.path.find( "budget" ) != std::string::npos );
    }
}

TEST_CASE( "evidence serde round-trips and validates", "[grader][schema][evidence]" )
{
    auto err = noopError();
    auto evidence = GradeEvidence::fromJson( evidenceJson(), err );
    REQUIRE( evidence.has_value() );
    CHECK( evidence->items.size() == 1 );
    CHECK( evidence->items[0].kind == EvidenceKind::Stage );
    CHECK( evidence->validate( err ) );

    Json::Value doc2 = evidence->toJson();
    auto evidence2 = GradeEvidence::fromJson( doc2, err );
    REQUIRE( evidence2.has_value() );
    REQUIRE( evidence2->items.size() == 1 );
    CHECK( evidence2->items[0].evidenceId == "ev-1" );

    SECTION( "foreign schema refused" )
    {
        err = {};
        GradeEvidence::fromJson( evidenceJson( "sicnu.grader.evidence/9" ), err );
        CHECK( err.code == GraderErrorCode::SchemaVersionUnsupported );
    }

    SECTION( "duplicate evidenceId refused" )
    {
        Json::Value doc = evidenceJson();
        doc["items"].append( doc["items"][0] );
        err = {};
        CHECK_FALSE( GradeEvidence::fromJson( doc, err ).has_value() );
        CHECK( err.message.find( "ev-1" ) != std::string::npos );
        CHECK( err.path.find( "evidenceId" ) != std::string::npos );
    }

    SECTION( "unknown evidence kind refused" )
    {
        Json::Value doc = evidenceJson();
        doc["items"][0]["kind"] = "button_click";
        err = {};
        CHECK_FALSE( GradeEvidence::fromJson( doc, err ).has_value() );
    }

    SECTION( "metric item needs a finite value" )
    {
        Json::Value doc = evidenceJson();
        Json::Value metric{ Json::objectValue };
        metric["evidenceId"] = "ev-2";
        metric["kind"] = "metric";
        metric["key"] = "overall_accuracy";
        metric["value"] = std::nan( "" );
        doc["items"].append( metric );
        err = {};
        CHECK_FALSE( GradeEvidence::fromJson( doc, err ).has_value() );
        CHECK( err.message.find( "ev-2" ) != std::string::npos );
    }

    SECTION( "answer item needs answerText member" )
    {
        Json::Value doc = evidenceJson();
        Json::Value answer{ Json::objectValue };
        answer["evidenceId"] = "ev-3";
        answer["kind"] = "answer";
        answer["key"] = "q1";
        answer["facts"] = Json::Value{ Json::objectValue };
        doc["items"].append( answer );
        err = {};
        CHECK_FALSE( GradeEvidence::fromJson( doc, err ).has_value() );
        CHECK( err.path.find( "answerText" ) != std::string::npos );
    }

    SECTION( "oversized answer refused" )
    {
        Json::Value doc = evidenceJson();
        Json::Value answer{ Json::objectValue };
        answer["evidenceId"] = "ev-3";
        answer["kind"] = "answer";
        answer["key"] = "q1";
        answer["facts"] = Json::Value{ Json::objectValue };
        answer["facts"]["answerText"] = std::string( 65 * 1024, 'x' );
        doc["items"].append( answer );
        err = {};
        CHECK_FALSE( GradeEvidence::fromJson( doc, err ).has_value() );
    }

    SECTION( "empty answerText is legal (student submitted nothing)" )
    {
        Json::Value doc = evidenceJson();
        Json::Value answer{ Json::objectValue };
        answer["evidenceId"] = "ev-3";
        answer["kind"] = "answer";
        answer["key"] = "q1";
        answer["facts"] = Json::Value{ Json::objectValue };
        answer["facts"]["answerText"] = "";
        doc["items"].append( answer );
        err = {};
        auto evidence3 = GradeEvidence::fromJson( doc, err );
        REQUIRE( evidence3.has_value() );
        CHECK( evidence3->validate( err ) );
    }
}

TEST_CASE( "report digest binds the body and detects tampering", "[grader][schema][report]" )
{
    auto err = noopError();
    GradeReport report;
    report.subject.experimentId = "exp-1";
    report.rubricId = "lab03-supervised";
    report.rubricRevision = 1;
    report.evidenceDigest = "deadbeef";
    report.score = 87.5;
    report.totalPoints = 100.0;
    report.passingScore = 60.0;
    report.verdict = ReportVerdict::Pass;

    Json::Value doc = report.toJson();
    REQUIRE( doc["digest"].asString().size() == 64 );

    auto parsed = GradeReport::fromJson( doc, err );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->verifyDigest( err ) );
    // Byte-stable re-serialization.
    CHECK( canonicalizeJson( parsed->toJson(), err ).value() == canonicalizeJson( doc, err ).value() );

    SECTION( "tampered score refuses verification" )
    {
        parsed->score = 99.0;
        err = {};
        CHECK_FALSE( parsed->verifyDigest( err ) );
        CHECK( toCodeString( err.code ) == "grader:e-schema-shape" );
    }

    SECTION( "missing digest refuses verification" )
    {
        Json::Value tampered = doc;
        tampered.removeMember( "digest" );
        auto noDigest = GradeReport::fromJson( tampered, err );
        REQUIRE( noDigest.has_value() );
        err = {};
        CHECK_FALSE( noDigest->verifyDigest( err ) );
    }

    SECTION( "foreign schema refused" )
    {
        err = {};
        Json::Value wrong = doc;
        wrong["schema"] = "sicnu.grader.report/2";
        CHECK_FALSE( GradeReport::fromJson( wrong, err ).has_value() );
        CHECK( err.code == GraderErrorCode::SchemaVersionUnsupported );
    }
}

TEST_CASE( "engine refuses invalid documents without producing a report", "[grader][schema][engine]" )
{
    auto err = noopError();
    auto badRubric = GradingRubric::fromJson( rubricJson( "sicnu.grader.rubric/2" ), err );
    CHECK_FALSE( badRubric.has_value() );

    // Value-object path: grade() validates before anything else.
    GradingRubric rubric;
    rubric.rubricId = "x";
    rubric.revision = 0; // invalid
    rubric.totalPoints = 100.0;
    GradeEvidence evidence;
    evidence.items.push_back( GradeEvidenceItem{} );
    auto outcome = grade( rubric, evidence );
    CHECK_FALSE( outcome.ok );
    CHECK_FALSE( outcome.error.ok() );
    CHECK( outcome.error.code != GraderErrorCode::Internal );
}

// ---------------------------------------------------------------------------
// Hostile shapes: typed refusal, never an escaping exception (and no
// fabricated digest over an uncanonicalizable body).
// hardening/verifier-grader-explain-evidence — fail-closed contract.
// ---------------------------------------------------------------------------

TEST_CASE( "hostile documents are typed refusals, never exceptions", "[grader][schema][hostile]" )
{
    auto err = noopError();

    SECTION( "rubric schema member of array type" )
    {
        Json::Value doc = rubricJson();
        doc["schema"] = Json::Value{ Json::arrayValue };
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "rubric schema member of object type" )
    {
        Json::Value doc = rubricJson();
        doc["schema"] = Json::Value{ Json::objectValue };
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
    }

    SECTION( "budgets.maxCriteria of string type" )
    {
        Json::Value doc = rubricJson();
        doc["budgets"] = Json::Value{ Json::objectValue };
        doc["budgets"]["maxCriteria"] = "many";
        doc["budgets"]["maxEvidenceItems"] = Json::Value{ Json::arrayValue };
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "evidence schema member of array type" )
    {
        Json::Value doc = evidenceJson();
        doc["schema"] = Json::Value{ Json::arrayValue };
        REQUIRE_NOTHROW( GradeEvidence::fromJson( doc, err ) );
        CHECK_FALSE( GradeEvidence::fromJson( doc, err ).has_value() );
    }

    SECTION( "report schema member of object type" )
    {
        GradeReport report;
        report.rubricId = "lab03-supervised";
        report.rubricRevision = 1;
        report.score = 10.0;
        report.totalPoints = 100.0;
        report.passingScore = 60.0;
        report.verdict = ReportVerdict::Fail;
        Json::Value doc = report.toJson();
        doc["schema"] = Json::Value{ Json::objectValue };
        REQUIRE_NOTHROW( GradeReport::fromJson( doc, err ) );
        CHECK_FALSE( GradeReport::fromJson( doc, err ).has_value() );
    }

    SECTION( "budgets.maxCriteria beyond int32 is a typed refusal" )
    {
        Json::Value doc = rubricJson();
        doc["budgets"] = Json::Value{ Json::objectValue };
        doc["budgets"]["maxCriteria"] = Json::Value( Json::Int64( 5000000000LL ) );
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.path == "budgets.maxCriteria" );
    }

    SECTION( "budgets.maxEvidenceItems that would wrap to 1 if truncated" )
    {
        // 2^32 + 1: naive Int64->int narrowing produces 1, which passes every
        // >= 1 budget check — the wrap must be refused instead.
        Json::Value doc = rubricJson();
        doc["budgets"] = Json::Value{ Json::objectValue };
        doc["budgets"]["maxEvidenceItems"] = Json::Value( Json::UInt64( 4294967297ULL ) );
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
    }

    SECTION( "budgets of non-object type is a typed refusal" )
    {
        Json::Value doc = rubricJson();
        doc["budgets"] = 5;
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
    }

    SECTION( "revision beyond int32 is a typed refusal" )
    {
        Json::Value doc = rubricJson();
        doc["revision"] = Json::Value( Json::Int64( 5000000000LL ) );
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
    }

    SECTION( "stage.minDistinct beyond int32 is a typed refusal" )
    {
        Json::Value doc = rubricJson();
        doc["dimensions"][0]["criteria"][0]["stage"]["minDistinct"] =
            Json::Value( Json::Int64( 5000000000LL ) );
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
    }

    SECTION( "fact.minCount beyond int32 is a typed refusal" )
    {
        Json::Value doc = rubricJson();
        Json::Value criterion{ Json::objectValue };
        criterion["criterionId"] = "fact-x";
        criterion["maxPoints"] = 100.0;
        criterion["kind"] = "fact";
        criterion["evidenceKey"] = "state";
        criterion["fact"] = Json::Value{ Json::objectValue };
        criterion["fact"]["minCount"] = Json::Value( Json::Int64( 5000000000LL ) );
        doc["dimensions"][0]["criteria"][0] = criterion;
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
    }

    SECTION( "requiredFacts holding an uncanonicalizable value is a typed refusal" )
    {
        // jsoncpp parses 1e999 to a non-finite double WITHOUT a parse error;
        // the silent canonicalization used to turn the fact into "".
        Json::Value doc = rubricJson();
        Json::Value criterion{ Json::objectValue };
        criterion["criterionId"] = "fact-x";
        criterion["maxPoints"] = 100.0;
        criterion["kind"] = "fact";
        criterion["evidenceKey"] = "state";
        criterion["fact"] = Json::Value{ Json::objectValue };
        criterion["fact"]["requiredFacts"] = Json::Value{ Json::objectValue };
        criterion["fact"]["requiredFacts"]["ratio"] = Json::Value( 1e999 );
        doc["dimensions"][0]["criteria"][0] = criterion;
        REQUIRE_NOTHROW( GradingRubric::fromJson( doc, err ) );
        CHECK_FALSE( GradingRubric::fromJson( doc, err ).has_value() );
        CHECK( err.message.find( "ratio" ) != std::string::npos );
    }

    SECTION( "report rubricRef.revision beyond int32 is a typed refusal" )
    {
        GradeReport report;
        report.rubricId = "lab03-supervised";
        report.rubricRevision = 1;
        report.score = 10.0;
        report.totalPoints = 100.0;
        report.passingScore = 60.0;
        report.verdict = ReportVerdict::Fail;
        Json::Value doc = report.toJson();
        doc["rubricRef"]["revision"] = Json::Value( Json::UInt64( 9999999999ULL ) );
        REQUIRE_NOTHROW( GradeReport::fromJson( doc, err ) );
        CHECK_FALSE( GradeReport::fromJson( doc, err ).has_value() );
    }

    SECTION( "report hostile score type is a refusal, not a silent zero" )
    {
        GradeReport report;
        report.rubricId = "lab03-supervised";
        report.rubricRevision = 1;
        report.score = 87.5;
        report.totalPoints = 100.0;
        report.passingScore = 60.0;
        report.verdict = ReportVerdict::Pass;
        Json::Value doc = report.toJson();
        doc["score"] = "abc";
        REQUIRE_NOTHROW( GradeReport::fromJson( doc, err ) );
        CHECK_FALSE( GradeReport::fromJson( doc, err ).has_value() );
    }

    SECTION( "parseJsonStrict survives a deep nesting bomb as typed refusal" )
    {
        const std::string bomb( 40000, '[' );
        err = {};
        REQUIRE_FALSE( parseJsonStrict( bomb, err ).has_value() );
        CHECK( err.code == GraderErrorCode::InvalidJson );
    }
}

TEST_CASE( "report over an uncanonicalizable body carries no fabricated digest",
           "[grader][schema][hostile]" )
{
    auto err = noopError();
    GradeReport report;
    report.subject.experimentId = "exp-1";
    report.rubricId = "lab03-supervised";
    report.rubricRevision = 1;
    report.score = std::nan( "" ); // non-finite: canonicalization must refuse
    report.totalPoints = 100.0;
    report.passingScore = 60.0;
    report.verdict = ReportVerdict::Fail;

    const Json::Value doc = report.toJson();
    // The fabricated-digest fail-open: sha256("") used to be embedded for a
    // body that cannot be canonicalized — a digest that can never verify.
    const std::string digest = doc["digest"].asString();
    CHECK( digest.empty() );
    CHECK_FALSE( report.verifyDigest( err ) );

    // Well-formed reports keep the sealed-digest behavior (the pre-existing
    // digest-binding test pins that surface).
}

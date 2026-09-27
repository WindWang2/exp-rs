/***************************************************************************
  tests/test_teaching_parity_r4.cpp — WP-C (Track 12, teaching-lab R4):
  transcript PARITY residuals + session-family failure paths.

  Parity truth comes from REPLAY (documents fed twice / permuted / threaded
  and compared), never from the projection's own logic re-derived. The
  session-family failure paths (lab_session_state strict parse,
  lab_step_timeline unknown document, lab_feedback_projection wrong-schema
  restore) are asserted on the public static seams only.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>
#include <json/writer.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_session_state.h"
#include "teaching/lab_step_timeline.h"

using namespace sicnu::teaching;

namespace
{

Json::Value parse( const std::string &text )
{
    Json::Value doc;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream stream( text );
    const bool ok = Json::parseFromStream( b, stream, &doc, &errs );
    if ( !ok )
        FAIL( errs );
    return doc;
}

/// A small but complete transcript pair: unified verifier report + a
/// sicnu.lab.grade/1 body with per-assertion evidence and deductions.
struct TranscriptPair
{
    Json::Value verifier = parse( R"JSON({
      "schema": "sicnu.verification.report/1",
      "overall": "pass",
      "specId": "lab90_r4",
      "checks": [
        { "checkId": "c1", "status": "pass", "message": "crs ok" },
        { "checkId": "c2", "status": "indeterminate", "message": "no stats" }
      ]
    })JSON" );

    Json::Value grader = parse( R"JSON({
      "overall": "pass",
      "score": 88.5,
      "passing_score": 60,
      "evidence": [
        { "assertion_id": "a1", "passed": true },
        { "assertion_id": "a2", "passed": false },
        { "assertion_id": "a3", "passed": true }
      ],
      "deductions": [
        { "assertion_id": "a2", "message": "band math diverges" }
      ]
    })JSON" );
};

} // namespace

TEST_CASE( "parity r4: restored feedback summary round-trips byte-stable",
           "[teaching_r4][parity]" )
{
    TranscriptPair t;
    const LabFeedbackProjection live =
      LabFeedbackProjection::fromReports( "lab90_r4", t.verifier, t.grader, "capsule://x" );
    REQUIRE( live.ok );
    // The grader's own evidence carries a real fail (a2) — fail dominates the
    // verifier's indeterminate, and an indeterminate check kept it from pass.
    CHECK( live.overallStatus == "fail" );
    CHECK_FALSE( live.overallCountsAsPass );

    const Json::Value doc1 = live.toJson();
    const LabFeedbackProjection restored = LabFeedbackProjection::fromJson( doc1 );
    REQUIRE( restored.ok );
    const Json::Value doc2 = restored.toJson();
    // Byte-level stability, not merely structural equality: the persisted
    // document re-serializes to identical bytes after a restart.
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    const std::string bytes1 = Json::writeString( b, doc1 );
    const std::string bytes2 = Json::writeString( b, doc2 );
    CHECK( bytes1 == bytes2 );
    CHECK( doc2 == doc1 );
    CHECK( restored.overallStatus == live.overallStatus );
    CHECK( restored.overallCountsAsPass == live.overallCountsAsPass );
    CHECK( restored.graderScore == live.graderScore );
}

TEST_CASE( "parity r4: projection is invariant under transcript array order",
           "[teaching_r4][parity]" )
{
    TranscriptPair t;

    const LabFeedbackProjection baseline =
      LabFeedbackProjection::fromReports( "lab90_r4", t.verifier, t.grader, {} );

    // Permute evidence[] and checks[] (event replay in a different arrival
    // order): the per-assertion verdicts must follow their assertion ids and
    // the aggregate must not move.
    Json::Value permutedVerifier = t.verifier;
    std::reverse( permutedVerifier["checks"].begin(), permutedVerifier["checks"].end() );
    Json::Value permutedGrader = t.grader;
    std::reverse( permutedGrader["evidence"].begin(), permutedGrader["evidence"].end() );

    const LabFeedbackProjection permuted = LabFeedbackProjection::fromReports(
      "lab90_r4", permutedVerifier, permutedGrader, {} );

    CHECK( permuted.overallStatus == baseline.overallStatus );
    CHECK( permuted.overallCountsAsPass == baseline.overallCountsAsPass );

    auto verdictSet = []( const LabFeedbackProjection &p ) {
        std::vector<std::pair<std::string, std::string>> v;
        for ( const auto &row : p.rows )
            v.emplace_back( row.id, row.status );
        std::sort( v.begin(), v.end() );
        return v;
    };
    CHECK( verdictSet( permuted ) == verdictSet( baseline ) );
}

TEST_CASE( "parity r4: partial grader failure keeps full transcript integrity",
           "[teaching_r4][parity]" )
{
    TranscriptPair t;
    // The grader refused mid-run: overall is indeterminate with an error,
    // but the evidence already produced must still project verbatim —
    // dropped rows would hide what the engine DID verify.
    t.grader["overall"] = "indeterminate";
    t.grader["error"] = "engine refused mid-run";

    const LabFeedbackProjection p =
      LabFeedbackProjection::fromReports( "lab90_r4", t.verifier, t.grader, {} );
    REQUIRE( p.ok );

    // Every assertion survives: a1 pass, a2 fail (+its deduction message),
    // a3 pass; the grader overall stays honestly indeterminate.
    std::vector<std::pair<std::string, std::string>> verdicts;
    for ( const auto &row : p.rows )
        if ( row.layer == "grader" && row.id.rfind( "a", 0 ) == 0 )
            verdicts.emplace_back( row.id, row.status );
    REQUIRE( verdicts.size() == 3 );
    CHECK( verdicts[0].second == "pass" );
    CHECK( verdicts[1].second == "fail" );
    CHECK( verdicts[2].second == "pass" );

    for ( const auto &row : p.rows )
    {
        if ( row.id == "a2" )
            CHECK( row.reasonZh == "band math diverges" );
        if ( row.id == "a1" )
            CHECK( row.reasonZh.empty() ); // pass rows carry no deduction text
    }

    CHECK( p.overallStatus == "fail" ); // real fails dominate the refusal
    CHECK_FALSE( p.overallCountsAsPass );
    bool refusalRecorded = false;
    for ( const auto &issue : p.issuesZh )
        if ( issue.find( "评分不可用" ) != std::string::npos )
            refusalRecorded = true;
    CHECK( refusalRecorded );
}

TEST_CASE( "parity r4: concurrent projections equal sequential replay (no cross-talk)",
           "[teaching_r4][parity]" )
{
    TranscriptPair t;
    // Four transcripts that differ only in score / verdict; projected from
    // four threads they must match the sequential results exactly.
    std::vector<Json::Value> graders;
    for ( int i = 0; i < 4; ++i )
    {
        Json::Value g = t.grader;
        g["score"] = 60.0 + i;
        graders.push_back( g );
    }

    std::vector<Json::Value> sequential;
    for ( const auto &g : graders )
        sequential.push_back(
          LabFeedbackProjection::fromReports( "lab90_r4", t.verifier, g, {} ).toJson() );

    std::vector<Json::Value> concurrent( graders.size() );
    std::vector<std::thread> pool;
    for ( std::size_t i = 0; i < graders.size(); ++i )
    {
        pool.emplace_back(
          [i, &t, &graders, &concurrent]() {
              concurrent[i] =
                LabFeedbackProjection::fromReports( "lab90_r4", t.verifier, graders[i], {} ).toJson();
          } );
    }
    for ( auto &thread : pool )
        thread.join();

    for ( std::size_t i = 0; i < graders.size(); ++i )
        CHECK( concurrent[i] == sequential[i] );
}

TEST_CASE( "feedback projection r4: wrong-schema restore is refused outright",
           "[teaching_r4][session][failclosed]" )
{
    const Json::Value forged = parse( R"JSON({
      "schema": "something.else/9",
      "overall_status": "pass",
      "overall_counts_as_pass": true
    })JSON" );

    const LabFeedbackProjection restored = LabFeedbackProjection::fromJson( forged );
    CHECK_FALSE( restored.ok );
    CHECK( restored.overallStatus == "indeterminate" );
    CHECK_FALSE( restored.overallCountsAsPass );
    CHECK_FALSE( restored.issuesZh.empty() );
}

TEST_CASE( "session state r4: well-formed but contract-violating documents are strictly refused",
           "[teaching_r4][session][failclosed]" )
{
    LabSessionState session = LabSessionState::makeNew( "sess-1", "course-1", "lab90" );
    session.stepIndex = 2;
    Json::Value doc = session.toJson();

    // An unknown key (e.g. a golden value smuggled into the state file) must
    // refuse the WHOLE document, not adopt the rest.
    doc["score_golden"] = 100.0;
    const LabSessionState smuggled = LabSessionState::fromJson( doc );
    CHECK_FALSE( smuggled.ok );
    REQUIRE_FALSE( smuggled.issuesZh.empty() );
    CHECK( smuggled.issuesZh.front().find( "未知字段" ) != std::string::npos );

    // Negative step index: type-correct, contract-illegal.
    Json::Value negative = session.toJson();
    negative["step_index"] = -1;
    const LabSessionState illegal = LabSessionState::fromJson( negative );
    CHECK_FALSE( illegal.ok );

    // Wrong-typed artifact path.
    Json::Value wrongType = session.toJson();
    wrongType["artifact_path"] = 42;
    const LabSessionState mistyped = LabSessionState::fromJson( wrongType );
    CHECK_FALSE( mistyped.ok );
}

TEST_CASE( "session state r4: serialize round-trip keeps transcript continuity refs",
           "[teaching_r4][session]" )
{
    LabSessionState session = LabSessionState::makeNew( "sess-1", "course-1", "lab90" );
    session.stepIndex = 3;
    session.evidenceRefs = { "evidence://run1/ndvi.tif", "evidence://run1/report.json" };
    session.artifactPath = "runs/sess-1/artifact.tif";
    session.capsuleExportRef = "capsule://sess-1";
    session.lastValidationSummary["overall_status"] = "indeterminate";

    const std::string bytes = session.serialize();
    const LabSessionState restored = LabSessionState::deserialize( bytes );
    REQUIRE( restored.ok );
    CHECK( restored.stepIndex == 3 );
    CHECK( restored.evidenceRefs == session.evidenceRefs );
    CHECK( restored.artifactPath == session.artifactPath );
    CHECK( restored.capsuleExportRef == session.capsuleExportRef );
    CHECK( restored.lastValidationSummary == session.lastValidationSummary );
}

TEST_CASE( "step timeline r4: unrecognizable lab document fails closed as unknown",
           "[teaching_r4][timeline][failclosed]" )
{
    const Json::Value alien = parse( R"JSON({ "schema": "whatever/9", "title": "x" })JSON" );
    const LabStepTimeline timeline = LabStepTimeline::fromLabDocument( alien, 0 );
    CHECK_FALSE( timeline.ok );
    CHECK( timeline.sourceKind == "unknown" );
    CHECK( timeline.steps.empty() );
    CHECK_FALSE( timeline.issuesZh.empty() );

    const Json::Value noSteps = parse( R"JSON({
      "spec_version": 1, "id": "lab90", "title": "t"
    })JSON" );
    const LabStepTimeline broken = LabStepTimeline::fromLabDocument( noSteps, 0 );
    CHECK_FALSE( broken.ok );
    CHECK_FALSE( broken.issuesZh.empty() );
}

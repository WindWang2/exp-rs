/***************************************************************************
  tests/test_teaching_restore_r16_c_t05.cpp — restart-path fail-closed pins.

  The feedback summary is persisted into the teaching session file and
  re-materialized through LabFeedbackProjection::fromJson on restart. The
  header contract is per-row: `countsAsPass` is NEVER true unless the row's
  own status is "pass" (computed, not asserted, on the fromReports path).
  A hand-edited session file must not smuggle a counted-as-pass row past
  the restore sweep — the same discipline the overall flag already has.
 ***************************************************************************/

#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_session_state.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <unistd.h>

#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>

using namespace sicnu::teaching;

namespace {

Json::Value parse( const std::string &text )
{
  Json::Value root;
  Json::CharReaderBuilder b;
  std::string errs;
  std::istringstream ss( text );
  REQUIRE( Json::parseFromStream( b, ss, &root, &errs ) );
  return root;
}

/// sicnu.lab.grade/1 transcript with one failing assertion — the canonical
/// way a student produces a "fail" feedback summary worth forging.
Json::Value failingGradeTranscript()
{
  return parse( R"({
    "verdict": "fail",
    "score": 40.0,
    "passing_score": 60.0,
    "deductions": [ { "assertion_id": "crs_match", "kind": "identity",
                      "severity": "blocking", "weight": 40.0,
                      "message": "CRS 与要求不一致" } ],
    "evidence": [ { "assertion_id": "crs_match", "kind": "identity",
                    "passed": false } ]
  })" );
}

} // namespace

TEST_CASE( "r16-c-t05: a forged row counts_as_pass never survives restore",
           "[teaching][feedback][restore][r16_c_t05]" )
{
  const auto original =
    LabFeedbackProjection::fromReports( "lab12_sar_processing", Json::Value(),
                                        failingGradeTranscript() );
  REQUIRE( original.ok );
  REQUIRE( original.overallStatus == "fail" );
  REQUIRE_FALSE( original.overallCountsAsPass );

  // Restart: serialize, hand-edit every row's counts_as_pass to true (the
  // on-disk session file is untrusted input), restore.
  Json::Value doc = original.toJson();
  REQUIRE( doc["rows"].isArray() );
  REQUIRE( doc["rows"].size() >= 2 );
  for ( auto &row : doc["rows"] ) {
    REQUIRE( row["status"].asString() != "pass" );
    row["counts_as_pass"] = true;
  }
  const auto restored = LabFeedbackProjection::fromJson( doc );
  REQUIRE( restored.ok );
  for ( const auto &row : restored.rows ) {
    // The recorded verdict (fail) is kept — but a non-pass row must never
    // be counted as pass, whatever the persisted flag claims.
    REQUIRE( row.status != "pass" );
    REQUIRE_FALSE( row.countsAsPass );
  }
  REQUIRE_FALSE( restored.overallCountsAsPass );

  // The restored summary re-serializes without the forged flags (the
  // contradiction must not propagate into the next save cycle).
  const auto reserialized = restored.toJson();
  for ( const auto &row : reserialized["rows"] )
    if ( row["status"].asString() != "pass" )
      REQUIRE_FALSE( row["counts_as_pass"].asBool() );
}

TEST_CASE( "r16-c-t05: restored rows with non-enum statuses never count as pass",
           "[teaching][feedback][restore][r16_c_t05]" )
{
  // "not_earned" is in the documented row vocabulary; "banana" is not —
  // neither may carry a counted-as-pass flag off disk.
  const Json::Value doc = parse( R"({
    "schema": "sicnu.teaching.lab_feedback/1",
    "lab_id": "lab15_data_inspection",
    "overall_status": "fail",
    "rows": [
      { "id": "r1", "layer": "grader", "status": "not_earned",
        "status_zh": "未通过", "reason_zh": "x", "counts_as_pass": true },
      { "id": "r2", "layer": "verifier", "status": "banana",
        "status_zh": "?", "reason_zh": "y", "counts_as_pass": true },
      { "id": "r3", "layer": "grader", "status": "pass",
        "status_zh": "通过", "reason_zh": "z", "counts_as_pass": true }
    ]
  })" );
  const auto restored = LabFeedbackProjection::fromJson( doc );
  REQUIRE( restored.ok );
  REQUIRE( restored.rows.size() == 3 );
  REQUIRE_FALSE( restored.rows[0].countsAsPass );
  REQUIRE_FALSE( restored.rows[1].countsAsPass );
  REQUIRE( restored.rows[2].countsAsPass ); // a genuine pass row keeps its flag
}

TEST_CASE( "r16-c-t05: overall counts_as_pass forgery stays refused on the right schema",
           "[teaching][feedback][restore][r16_c_t05]" )
{
  // Right-schema document claiming overall pass-ness on a fail verdict —
  // pin the existing overall gate alongside the row-level sweep above.
  const Json::Value doc = parse( R"({
    "schema": "sicnu.teaching.lab_feedback/1",
    "lab_id": "lab15_data_inspection",
    "overall_status": "fail",
    "overall_counts_as_pass": true,
    "rows": []
  })" );
  const auto restored = LabFeedbackProjection::fromJson( doc );
  REQUIRE( restored.ok );
  REQUIRE( restored.overallStatus == "fail" );
  REQUIRE_FALSE( restored.overallCountsAsPass );
}

TEST_CASE( "r16-c-t05: teaching session state round-trips under a unicode path",
           "[teaching][session][r16_c_t05][verification]" )
{
  // The cockpit resolves the session file under AppDataLocation, which on a
  // Chinese-locale home contains non-ASCII components; the sidecar write
  // must stay byte-stable there and reload the identical state.
  const std::string dir = "/tmp/r16_c_t05_教学会话_" + std::to_string( ::getpid() ) + "_" +
                          std::to_string( ::clock() );
  std::filesystem::create_directories( dir );
  const std::string path = dir + "/lab_cockpit_session.json";

  auto session = LabSessionState::makeNew( "local/lab12", "undergraduate_rs",
                                           "lab12_sar_processing" );
  REQUIRE( session.ok );
  session.stepIndex = 3;
  session.evidenceRefs.push_back( "human:reflect:128" );
  REQUIRE( session.saveToFile( path ) );

  const auto restored = LabSessionState::loadFromFile( path );
  REQUIRE( restored.ok );
  REQUIRE( restored.sessionId == session.sessionId );
  REQUIRE( restored.stepIndex == 3 );
  REQUIRE( restored.evidenceRefs == session.evidenceRefs );

  std::filesystem::remove_all( dir );
}

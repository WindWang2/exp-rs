/***************************************************************************
  tests/test_r18_e_completion_ledger.cpp — R18-E E01 completion authority.

  T103 (D1, teaching half): completion is recorded only from a VERIFIED
  pass verdict — a session whose verdict stamp bound the artifact bytes
  (verdict_stamp.h). An unstamped verdict is a claim, not evidence; an
  entry whose session later disagrees with the recorded digest is demoted
  on load; a copy-pasted duplicate verdict across two labs is refused.
  The ledger projects into the sicnu.curriculum.progress/1 document the
  course home already consumes. (Dock wiring is cross-track: see
  CROSS_TRACK_FINDINGS.md CT-1.)
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching/lab_completion_ledger.h"
#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_session_state.h"

#include <json/json.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace sicnu::teaching;

namespace
{

Json::Value parse( const std::string &text )
{
  Json::Value root;
  Json::CharReaderBuilder b;
  std::string errs;
  std::istringstream ss( text );
  REQUIRE( Json::parseFromStream( b, ss, &root, &errs ) );
  return root;
}

Json::Value passingVerifierReport()
{
  return parse( R"({
    "schema": "sicnu.verification.report/1",
    "overall": "pass",
    "specId": "lab12.spec"
  })" );
}

Json::Value passingGradeTranscript()
{
  return parse( R"({
    "verdict": "pass",
    "score": 90.0,
    "passing_score": 60.0,
    "deductions": [],
    "evidence": [ { "assertion_id": "band_count", "kind": "identity",
                    "passed": true } ]
  })" );
}

std::filesystem::path tempDir( const char *tag )
{
  auto dir = std::filesystem::temp_directory_path()
    / ( std::string( "r18e_ledger_" ) + tag + "_" + std::to_string( ::getpid() ) );
  std::filesystem::remove_all( dir );
  std::filesystem::create_directories( dir );
  return dir;
}

void writeBytes( const std::filesystem::path &p, const std::string &bytes )
{
  std::ofstream out( p, std::ios::binary );
  out << bytes;
}

/// A session with a stamped pass verdict, saved to disk.
std::string stampedPassSession( const std::filesystem::path &dir, const std::string &labId )
{
  const auto artifact = dir / ( labId + ".tif" );
  const auto sessionPath = dir / ( labId + ".session.json" );
  writeBytes( artifact, "graded bytes for " + labId );

  auto fb = LabFeedbackProjection::fromReports( labId, passingVerifierReport(),
                                                passingGradeTranscript() );
  REQUIRE( fb.overallStatus == "pass" );
  auto s = LabSessionState::makeNew( "s1", "c1", labId );
  s.artifactPath = artifact.string();
  s.lastValidationSummary = fb.toJson();
  s.labStatus = LabUiStatus::Completed;
  REQUIRE( s.saveToFile( sessionPath.string() ) );
  return sessionPath.string();
}

} // namespace

TEST_CASE( "r18e T103: a stamped pass verdict becomes completion evidence",
           "[teaching][ledger][r18_e]" )
{
  const auto dir = tempDir( "record" );
  const std::string sessionRef = stampedPassSession( dir, "lab12" );

  const LabSessionState session = LabSessionState::loadFromFile( sessionRef );
  REQUIRE( session.ok );
  REQUIRE( !session.verdictArtifactStamp.empty() );

  LabCompletionLedger ledger;
  REQUIRE( ledger.recordVerdict( session, sessionRef ) );
  REQUIRE( ledger.completed.size() == 1 );
  REQUIRE( ledger.completed[0].labId == "lab12" );
  REQUIRE( ledger.completed[0].verdictDigest.size() == 64 );

  // The progress projection feeds the existing course-home consumer.
  const Json::Value progress = ledger.toProgressDoc();
  REQUIRE( progress["schema"].asString() == "sicnu.curriculum.progress/1" );
  REQUIRE( progress["completed"].isObject() );
  REQUIRE( progress["completed"].isMember( "lab12" ) );
  REQUIRE( progress["completed"]["lab12"]["evidence"].asString() == sessionRef );

  // Round trip + verified reload keeps the entry.
  const auto ledgerPath = dir / "ledger.json";
  {
    LabCompletionLedger saved = ledger;
    saved.ok = true;
    REQUIRE( saved.saveToFile( ledgerPath.string() ) );
  }
  const auto reloaded = LabCompletionLedger::loadVerified( ledgerPath.string() );
  CHECK( reloaded.ok );
  CHECK( reloaded.completed.size() == 1 );
  CHECK( reloaded.completed[0].labId == "lab12" );

  std::filesystem::remove_all( dir );
}

TEST_CASE( "r18e T103b: an unstamped verdict is a claim, not completion",
           "[teaching][ledger][r18_e]" )
{
  auto fb = LabFeedbackProjection::fromReports( "lab12", passingVerifierReport(),
                                                Json::Value() );
  auto s = LabSessionState::makeNew( "s1", "c1", "lab12" );
  s.lastValidationSummary = fb.toJson();
  s.labStatus = LabUiStatus::Completed;

  LabCompletionLedger ledger;
  CHECK_FALSE( ledger.recordVerdict( s, "/nowhere.json" ) );
  CHECK( ledger.completed.empty() );
  CHECK_FALSE( ledger.issuesZh.empty() );
}

TEST_CASE( "r18e T103c: a tampered ledger never survives as completion truth",
           "[teaching][ledger][r18_e]" )
{
  const auto dir = tempDir( "tamper" );
  const std::string sessionRef = stampedPassSession( dir, "lab12" );

  // A ledger claiming a SECOND lab completed with the SAME verdict
  // document — a copy-paste forgery; the loader refuses to arbitrate.
  LabCompletionLedger ledger;
  REQUIRE( ledger.recordVerdict( LabSessionState::loadFromFile( sessionRef ), sessionRef ) );
  Json::Value doc = ledger.toJson();
  doc["completed"]["lab99_other"] = doc["completed"]["lab12"];
  const auto ledgerPath = dir / "ledger.json";
  {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    std::ofstream out( ledgerPath );
    out << Json::writeString( wb, doc );
  }
  const auto refused = LabCompletionLedger::loadVerified( ledgerPath.string() );
  CHECK_FALSE( refused.ok );

  // A session that moved on (re-validated fail over the same artifact)
  // demotes the stale completion entry instead of keeping it.
  const auto artifact = dir / "lab12.tif";
  writeBytes( artifact, "re-submitted different bytes" );
  {
    auto fb = LabFeedbackProjection::fromReports( "lab12", Json::Value(), Json::Value() );
    auto s = LabSessionState::loadFromFile( sessionRef );
    REQUIRE( s.ok );
    // Downgraded by the stamp check; re-save with a new fail verdict.
    auto fresh = LabSessionState::makeNew( "s1", "c1", "lab12" );
    fresh.artifactPath = artifact.string();
    fresh.lastValidationSummary = fb.toJson();
    fresh.labStatus = LabUiStatus::PendingVerify;
    REQUIRE( fresh.saveToFile( sessionRef ) );
  }
  const auto demoted = LabCompletionLedger::loadVerified( ledgerPath.string() );
  CHECK( demoted.completed.empty() );
  CHECK_FALSE( demoted.issuesZh.empty() );

  std::filesystem::remove_all( dir );
}

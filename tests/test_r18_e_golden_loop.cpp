/***************************************************************************
  tests/test_r18_e_golden_loop.cpp — R18-E E10: the closed feedback loop.

  One student, one lab, end to end, on the light (Qt-free) closure:

    artifact written → validation verdict pass recorded in the session →
    session stamped and saved → RESTART keeps the pass (bytes unchanged) →
    completion ledger records the verified verdict → progress projection
    feeds the course home → a re-submission (bytes changed) after restart
    downgrades the verdict to indeterminate and the ledger entry demotes.

  Every step asserts the STATE TRUTH, not just absence of crashes: pass
  stays pass only while the evidence holds; the moment it cannot be
  re-derived, nothing renders green.
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

std::filesystem::path tempDir()
{
  auto dir = std::filesystem::temp_directory_path()
    / ( "r18e_golden_" + std::to_string( ::getpid() ) );
  std::filesystem::remove_all( dir );
  std::filesystem::create_directories( dir );
  return dir;
}

void writeBytes( const std::filesystem::path &p, const std::string &bytes )
{
  std::ofstream out( p, std::ios::binary );
  out << bytes;
}

} // namespace

TEST_CASE( "r18e golden loop: verified pass survives restart, evidence loss never stays green",
           "[teaching][golden][r18_e]" )
{
  const auto dir = tempDir();
  const auto artifact = dir / "ndvi.tif";
  const auto sessionPath = dir / "session.json";
  const auto ledgerPath = dir / "ledger.json";

  // 1. The student produces an artifact and validates it green.
  writeBytes( artifact, "the graded ndvi bytes" );
  auto fb = LabFeedbackProjection::fromReports(
    "lab03_ndvi", parse( R"({
      "schema": "sicnu.verification.report/1",
      "overall": "pass",
      "specId": "lab03.spec"
    })" ),
    parse( R"({
      "verdict": "pass",
      "score": 90.0,
      "passing_score": 60.0,
      "deductions": [],
      "evidence": [ { "assertion_id": "band_count", "kind": "identity",
                      "passed": true } ]
    })" ) );
  REQUIRE( fb.ok );
  REQUIRE( fb.overallStatus == "pass" );

  auto session = LabSessionState::makeNew( "sess-1", "course-1", "lab03_ndvi" );
  session.artifactPath = artifact.string();
  session.lastValidationSummary = fb.toJson();
  session.labStatus = LabUiStatus::Completed;
  REQUIRE( session.saveToFile( sessionPath.string() ) );

  // 2. Restart: the recorded pass holds while the bytes are unchanged.
  auto restarted = LabSessionState::loadFromFile( sessionPath.string() );
  REQUIRE( restarted.ok );
  CHECK( restarted.lastValidationSummary[ "overall_status" ].asString() == "pass" );
  CHECK( restarted.labStatus == LabUiStatus::Completed );
  CHECK( !restarted.verdictArtifactStamp.empty() );

  // 3. Completion authority: the verified verdict becomes course-home truth.
  LabCompletionLedger ledger;
  REQUIRE( ledger.recordVerdict( restarted, sessionPath.string() ) );
  REQUIRE( ledger.completed.size() == 1 );
  ledger.ok = true;
  REQUIRE( ledger.saveToFile( ledgerPath.string() ) );
  const auto loadedLedger = LabCompletionLedger::loadVerified( ledgerPath.string() );
  CHECK( loadedLedger.ok );
  CHECK( loadedLedger.completed.size() == 1 );

  // 4. The student re-submits (different bytes) and restarts again: the
  // recorded pass cannot be re-derived, so it renders indeterminate — and
  // the ledger entry demotes rather than certifying stale completion.
  writeBytes( artifact, "the RE-SUBMITTED ndvi bytes" );
  auto afterResubmit = LabSessionState::loadFromFile( sessionPath.string() );
  REQUIRE( afterResubmit.ok );
  CHECK( afterResubmit.lastValidationSummary[ "overall_status" ].asString()
         == "indeterminate" );
  CHECK( afterResubmit.labStatus != LabUiStatus::Completed );

  const auto demotedLedger = LabCompletionLedger::loadVerified( ledgerPath.string() );
  CHECK( demotedLedger.completed.empty() );
  CHECK_FALSE( demotedLedger.issuesZh.empty() );

  // 5. The re-validated artifact re-earns completion — the loop closes.
  writeBytes( artifact, "the RE-SUBMITTED ndvi bytes" ); // stable now
  auto fresh = LabSessionState::makeNew( "sess-1", "course-1", "lab03_ndvi" );
  fresh.artifactPath = artifact.string();
  fresh.lastValidationSummary = fb.toJson();
  fresh.labStatus = LabUiStatus::Completed;
  REQUIRE( fresh.saveToFile( sessionPath.string() ) );
  auto reopened = LabSessionState::loadFromFile( sessionPath.string() );
  REQUIRE( reopened.ok );
  CHECK( reopened.lastValidationSummary[ "overall_status" ].asString() == "pass" );

  std::filesystem::remove_all( dir );
}

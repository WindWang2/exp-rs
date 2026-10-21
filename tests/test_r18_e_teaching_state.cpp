/***************************************************************************
  tests/test_r18_e_teaching_state.cpp — R18-E E01/E02 state-truth lane.

  RED-first pins for the student session restart path (src/teaching):

    T101 (D6): the persisted `lab_status` is not an authority. On restore it
      must agree with the recorded validation summary — a hand-edited
      "completed" over an empty or failed summary is corrected, never
      adopted. The summary is the only verdict authority on this path.

    T102 (D7): the recorded verdict is bound to the artifact bytes it graded
      (verdict stamp, written by the session save authority). Restore
      re-derives the binding and downgrades the summary to indeterminate
      when the artifact changed or vanished, or when the summary was edited
      after stamping. Legacy sessions written before the stamp keep their
      recorded verdict (upgrade compatibility — a worse default would erase
      every existing student session on upgrade).

    T102b (D7 residual): LabFeedbackProjection::fromJson refuses a wholly
      hand-written pass document with zero rows — every real projection
      carries at least one row, so an empty pass is a forgery, not a record.
 ***************************************************************************/

#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_session_state.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
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

/// sicnu.verification.report/1 with overall pass — the shape the cockpit
/// persists after a green validation.
Json::Value passingVerifierReport()
{
  return parse( R"({
    "schema": "sicnu.verification.report/1",
    "overall": "pass",
    "specId": "lab12.spec"
  })" );
}

/// sicnu.lab.grade/1 transcript with one passing assertion — the grader
/// half of a green validation (a missing grader report folds the overall
/// verdict to indeterminate by design; both halves are always present in
/// a cockpit-recorded pass).
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

LabFeedbackProjection passingProjection()
{
  auto fb = LabFeedbackProjection::fromReports( "lab12", passingVerifierReport(),
                                                passingGradeTranscript() );
  REQUIRE( fb.ok );
  REQUIRE( fb.overallStatus == "pass" );
  REQUIRE( fb.overallCountsAsPass );
  return fb;
}

std::filesystem::path tempDir()
{
  auto dir = std::filesystem::temp_directory_path()
    / ( "r18e_teaching_" + std::to_string( ::getpid() ) + "_"
        + std::to_string( std::filesystem::temp_directory_path().string().size() )
        + std::to_string( __LINE__ ) );
  std::filesystem::create_directories( dir );
  return dir;
}

void writeBytes( const std::filesystem::path &p, const std::string &bytes )
{
  std::ofstream out( p, std::ios::binary );
  out << bytes;
}

Json::Value readJsonFile( const std::filesystem::path &p )
{
  std::ifstream in( p );
  Json::Value root;
  Json::CharReaderBuilder b;
  std::string errs;
  REQUIRE( Json::parseFromStream( b, in, &root, &errs ) );
  return root;
}

void writeJsonFile( const std::filesystem::path &p, const Json::Value &doc )
{
  Json::StreamWriterBuilder wb;
  wb["indentation"] = "  ";
  std::ofstream out( p );
  out << Json::writeString( wb, doc );
}

} // namespace

// ---------------------------------------------------------------------------
// T101 — lab_status is not an authority; the summary is
// ---------------------------------------------------------------------------

TEST_CASE( "r18e T101: lab_status completed over an empty summary is corrected on restore",
           "[teaching][session][restore][r18_e]" )
{
  auto s = LabSessionState::makeNew( "s1", "c1", "lab12" );
  REQUIRE( s.ok );
  Json::Value doc = s.toJson();
  // Hand-edit: completed with no recorded verdict (the D6 forgery).
  doc["lab_status"] = "completed";
  doc["last_validation_summary"] = Json::Value( Json::objectValue );

  const auto restored = LabSessionState::fromJson( doc );
  REQUIRE( restored.ok );
  CHECK( restored.labStatus != LabUiStatus::Completed );
  CHECK( !restored.issuesZh.empty() );
}

TEST_CASE( "r18e T101b: a fail summary keeps needs_correction (consistent restore)",
           "[teaching][session][restore][r18_e]" )
{
  auto fb = LabFeedbackProjection::fromReports( "lab12", Json::Value(),
                                                Json::Value() );
  REQUIRE( fb.overallStatus == "indeterminate" );
  auto s = LabSessionState::makeNew( "s1", "c1", "lab12" );
  Json::Value doc = s.toJson();
  doc["lab_status"] = "pending_verify";
  doc["last_validation_summary"] = fb.toJson();

  const auto restored = LabSessionState::fromJson( doc );
  REQUIRE( restored.ok );
  CHECK( restored.labStatus == LabUiStatus::PendingVerify );
}

TEST_CASE( "r18e T101c: the summary corrects a contradictory verdict status both ways",
           "[teaching][session][restore][r18_e]" )
{
  auto s = LabSessionState::makeNew( "s1", "c1", "lab12" );
  Json::Value doc = s.toJson();
  doc["lab_status"] = "needs_correction";
  doc["last_validation_summary"] = passingProjection().toJson();

  const auto restored = LabSessionState::fromJson( doc );
  REQUIRE( restored.ok );
  CHECK( restored.labStatus == LabUiStatus::Completed );
  CHECK( !restored.issuesZh.empty() );
}

// ---------------------------------------------------------------------------
// T102 — the verdict stamp binds the summary to the artifact bytes
// ---------------------------------------------------------------------------

TEST_CASE( "r18e T102: an unchanged artifact keeps its recorded pass across restart",
           "[teaching][session][verdict-stamp][r18_e]" )
{
  const auto dir = tempDir();
  const auto artifact = dir / "artifact.tif";
  const auto sessionPath = dir / "session.json";
  writeBytes( artifact, "the graded bytes" );

  auto s = LabSessionState::makeNew( "s1", "c1", "lab12" );
  s.artifactPath = artifact.string();
  s.lastValidationSummary = passingProjection().toJson();
  s.labStatus = LabUiStatus::Completed;
  REQUIRE( s.saveToFile( sessionPath.string() ) );

  // The save authority must record a non-empty binding.
  {
    const Json::Value saved = readJsonFile( sessionPath );
    REQUIRE( saved.isObject() );
    REQUIRE( saved.isMember( "verdict_artifact_stamp" ) );
    REQUIRE( saved[ "verdict_artifact_stamp" ].isString() );
    REQUIRE( !saved[ "verdict_artifact_stamp" ].asString().empty() );
  }

  const auto restored = LabSessionState::loadFromFile( sessionPath.string() );
  REQUIRE( restored.ok );
  REQUIRE( restored.lastValidationSummary.isObject() );
  CHECK( restored.lastValidationSummary[ "overall_status" ].asString() == "pass" );
  CHECK( restored.labStatus == LabUiStatus::Completed );

  std::filesystem::remove_all( dir );
}

TEST_CASE( "r18e T102b: a swapped artifact never keeps a recorded pass",
           "[teaching][session][verdict-stamp][r18_e]" )
{
  const auto dir = tempDir();
  const auto artifact = dir / "artifact.tif";
  const auto sessionPath = dir / "session.json";
  writeBytes( artifact, "the graded bytes" );

  auto s = LabSessionState::makeNew( "s1", "c1", "lab12" );
  s.artifactPath = artifact.string();
  s.lastValidationSummary = passingProjection().toJson();
  s.labStatus = LabUiStatus::Completed;
  REQUIRE( s.saveToFile( sessionPath.string() ) );

  // Re-submission: same name, different bytes.
  writeBytes( artifact, "different bytes — the student re-submitted" );

  const auto restored = LabSessionState::loadFromFile( sessionPath.string() );
  REQUIRE( restored.ok );
  CHECK( restored.lastValidationSummary[ "overall_status" ].asString()
         == "indeterminate" );
  CHECK( restored.labStatus != LabUiStatus::Completed );
  CHECK( !restored.issuesZh.empty() );

  std::filesystem::remove_all( dir );
}

TEST_CASE( "r18e T102c: a vanished artifact downgrades the verdict instead of faking pass",
           "[teaching][session][verdict-stamp][r18_e]" )
{
  const auto dir = tempDir();
  const auto artifact = dir / "artifact.tif";
  const auto sessionPath = dir / "session.json";
  writeBytes( artifact, "the graded bytes" );

  auto s = LabSessionState::makeNew( "s1", "c1", "lab12" );
  s.artifactPath = artifact.string();
  s.lastValidationSummary = passingProjection().toJson();
  s.labStatus = LabUiStatus::Completed;
  REQUIRE( s.saveToFile( sessionPath.string() ) );

  std::filesystem::remove( artifact );

  const auto restored = LabSessionState::loadFromFile( sessionPath.string() );
  REQUIRE( restored.ok );
  CHECK( restored.lastValidationSummary[ "overall_status" ].asString()
         == "indeterminate" );
  CHECK( !restored.issuesZh.empty() );

  std::filesystem::remove_all( dir );
}

TEST_CASE( "r18e T102d: a legacy session without a stamp keeps its recorded verdict",
           "[teaching][session][verdict-stamp][r18_e]" )
{
  auto s = LabSessionState::makeNew( "s1", "c1", "lab12" );
  Json::Value doc = s.toJson();
  doc["lab_status"] = "completed";
  doc["last_validation_summary"] = passingProjection().toJson();
  // Pre-stamp session files carry no verdict_artifact_stamp member at all.
  doc.removeMember( "verdict_artifact_stamp" );

  const auto restored = LabSessionState::fromJson( doc );
  REQUIRE( restored.ok );
  CHECK( restored.labStatus == LabUiStatus::Completed );
}

TEST_CASE( "r18e T102e: editing the summary after stamping voids it",
           "[teaching][session][verdict-stamp][r18_e]" )
{
  const auto dir = tempDir();
  const auto artifact = dir / "artifact.tif";
  const auto sessionPath = dir / "session.json";
  writeBytes( artifact, "the graded bytes" );

  auto fb = LabFeedbackProjection::fromReports( "lab12", Json::Value(),
                                                Json::Value() );
  REQUIRE( fb.overallStatus == "indeterminate" );
  auto s = LabSessionState::makeNew( "s1", "c1", "lab12" );
  s.artifactPath = artifact.string();
  s.lastValidationSummary = fb.toJson();
  s.labStatus = LabUiStatus::PendingVerify;
  REQUIRE( s.saveToFile( sessionPath.string() ) );

  // Tamper with the persisted file: rewrite the recorded summary into a
  // pass while keeping the artifact. The stamp binds the summary digest, so
  // a post-stamp edit voids the verdict instead of adopting the forgery.
  Json::Value doc = readJsonFile( sessionPath );
  REQUIRE( doc["last_validation_summary"].isObject() );
  doc["last_validation_summary"]["overall_status"] = "pass";
  doc["last_validation_summary"]["overall_counts_as_pass"] = true;
  doc["lab_status"] = "completed";
  writeJsonFile( sessionPath, doc );

  const auto restored = LabSessionState::loadFromFile( sessionPath.string() );
  REQUIRE( restored.ok );
  CHECK( restored.lastValidationSummary[ "overall_status" ].asString()
         != "pass" );
  CHECK( restored.labStatus != LabUiStatus::Completed );
  CHECK( !restored.issuesZh.empty() );

  std::filesystem::remove_all( dir );
}

// ---------------------------------------------------------------------------
// T102f — a wholly forged pass document with zero rows is refused
// ---------------------------------------------------------------------------

TEST_CASE( "r18e T102f: a hand-written pass summary with zero rows is not a pass",
           "[teaching][feedback][restore][r18_e]" )
{
  Json::Value doc = parse( R"({
    "schema": "sicnu.teaching.lab_feedback/1",
    "lab_id": "lab12",
    "overall_status": "pass",
    "overall_status_zh": "通过",
    "overall_counts_as_pass": true,
    "rows": []
  })" );
  const auto restored = LabFeedbackProjection::fromJson( doc );
  REQUIRE( restored.ok );
  CHECK( restored.overallStatus != "pass" );
  CHECK( !restored.overallCountsAsPass );
  CHECK( !restored.issuesZh.empty() );
}

/***************************************************************************
  tests/test_teaching_lab_cockpit.cpp — Undergraduate Lab Cockpit projections.

  Coverage (required):
   1 curriculum→course VM  2 lab→timeline  3 prereq blocked/warn/ready
   4 autonomy allow/deny/downgrade  5 human-only step  6 unknown operator
   7 missing dataset  8 scientific conflict  9 verifier indeterminate ≠ pass
  10 grader feedback projection  11 session save/reload
  12 corrupted session fail-closed  13 offline mode
  15 beginner/expert same underlying truth
 ***************************************************************************/

#include "teaching/autonomy_effective_display.h"
#include "teaching/course_home_view_model.h"
#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_readiness.h"
#include "teaching/lab_session_state.h"
#include "teaching/lab_status.h"
#include "teaching/lab_step_timeline.h"

#include "agent/autonomy/autonomy_capability.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace sicnu::teaching;
namespace fs = std::filesystem;

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

Json::Value minimalManifest()
{
  return parse( R"({
    "schema": "sicnu.curriculum/1",
    "id": "undergraduate_rs",
    "title_zh": "测试课程",
    "audience_zh": "测试听众",
    "modules": [
      {
        "id": "m01",
        "index": 1,
        "title_zh": "基础",
        "summary_zh": "s",
        "learning_outcomes": ["能读元数据"],
        "prerequisite_modules": [],
        "estimated_effort_minutes": 60,
        "optional": false,
        "labs": [
          {
            "lab_id": "lab15_data_inspection",
            "role": "core",
            "estimated_effort_minutes": 60,
            "required_data_packs": ["lab15_data_inspection"]
          }
        ]
      },
      {
        "id": "m02",
        "index": 2,
        "title_zh": "进阶",
        "summary_zh": "s2",
        "learning_outcomes": ["能做指数"],
        "prerequisite_modules": ["m01"],
        "estimated_effort_minutes": 90,
        "optional": false,
        "labs": [
          {
            "lab_id": "lab02_spectral_analysis",
            "role": "core",
            "estimated_effort_minutes": 90,
            "required_data_packs": ["lab02_spectral_analysis"]
          }
        ]
      }
    ]
  })" );
}

Json::Value availabilityOk()
{
  return parse( R"({
    "schema": "sicnu.curriculum.availability/1",
    "modules": [
      {
        "module_id": "m01",
        "labs": [
          {
            "lab_id": "lab15_data_inspection",
            "resolvable": "labspec",
            "operators": [
              {"operator_id": "rs:extract_bands", "state": "available"},
              {"operator_id": "rs:resample", "state": "registered_no_capability_note"}
            ],
            "data_packs": [{"name": "lab15_data_inspection", "present": true}]
          }
        ]
      },
      {
        "module_id": "m02",
        "labs": [
          {
            "lab_id": "lab02_spectral_analysis",
            "resolvable": "labspec",
            "operators": [{"operator_id": "rs:ndvi", "state": "available"}],
            "data_packs": [{"name": "lab02_spectral_analysis", "present": true}]
          }
        ]
      }
    ]
  })" );
}

} // namespace

TEST_CASE( "1 curriculum projects to course home VM", "[teaching][course]" )
{
  Json::Value progress( Json::objectValue );
  progress["schema"] = "sicnu.curriculum.progress.summary/1";
  progress["overall_percent"] = 0;
  progress["completed_lab_ids"] = Json::Value( Json::arrayValue );

  auto vm = CourseHomeViewModel::fromDocuments( minimalManifest(), progress, availabilityOk() );
  REQUIRE( vm.ok );
  REQUIRE( vm.courseId == "undergraduate_rs" );
  REQUIRE( vm.modules.size() == 2 );
  REQUIRE( vm.modules[0].labs.size() == 1 );
  REQUIRE( vm.modules[0].labs[0].labId == "lab15_data_inspection" );
  REQUIRE( vm.modules[0].labs[0].status == LabUiStatus::Ready );
  REQUIRE( !std::string( labUiStatusLabelZh( vm.modules[0].labs[0].status ) ).empty() );
  REQUIRE( !std::string( labUiStatusIconToken( vm.modules[0].labs[0].status ) ).empty() );
  REQUIRE( vm.continueLabId == "lab15_data_inspection" );
}

TEST_CASE( "2 lab document projects to step timeline", "[teaching][timeline]" )
{
  auto lab = parse( R"({
    "spec_version": 2,
    "id": "lab15_data_inspection",
    "title_zh": "数据体检",
    "objective_zh": "读元数据",
    "steps": [
      {
        "title_zh": "检查波段",
        "operator_id": "rs:extract_bands",
        "params": {"bands": [4, 5]},
        "teaching_note": "波段身份"
      },
      {
        "title_zh": "反思",
        "kind": "reflection",
        "teaching_note": "为什么要先读元数据"
      }
    ]
  })" );
  auto tl = LabStepTimeline::fromLabSpec( lab );
  REQUIRE( tl.ok );
  REQUIRE( tl.steps.size() == 2 );
  REQUIRE( tl.steps[0].operatorId == "rs:extract_bands" );
  REQUIRE( tl.steps[0].kind == "operator" );
  REQUIRE( tl.current() != nullptr );
  REQUIRE( tl.next() != nullptr );
  REQUIRE( tl.previous() == nullptr );
}

TEST_CASE( "3 prereq blocked / warn / ready", "[teaching][readiness][prereq]" )
{
  Json::Value progress( Json::objectValue );
  progress["schema"] = "sicnu.curriculum.progress.summary/1";
  progress["overall_percent"] = 0;
  progress["completed_lab_ids"] = Json::Value( Json::arrayValue );

  auto vm = CourseHomeViewModel::fromDocuments( minimalManifest(), progress, availabilityOk() );
  REQUIRE( vm.modules[1].labs[0].status == LabUiStatus::Unavailable );
  bool foundPrereq = false;
  for ( const auto &b : vm.modules[1].labs[0].blockersZh )
    if ( b.find( "先修模块" ) != std::string::npos ) foundPrereq = true;
  REQUIRE( foundPrereq );

  // Mark m01 done → m02 ready (with warn from capability note on m01 only).
  progress["completed_lab_ids"].append( "lab15_data_inspection" );
  progress["overall_percent"] = 50;
  auto vm2 = CourseHomeViewModel::fromDocuments( minimalManifest(), progress, availabilityOk() );
  REQUIRE( vm2.modules[0].labs[0].status == LabUiStatus::Completed );
  REQUIRE( vm2.modules[1].labs[0].status == LabUiStatus::Ready );

  // Warn path on readiness aggregator
  Json::Value slice = availabilityOk()["modules"][0]["labs"][0];
  Json::Value passport( Json::objectValue );
  passport["ok"] = true;
  Json::Value inspector( Json::objectValue );
  inspector["blocking"] = false;
  Json::Value sci( Json::objectValue );
  sci["conflict"] = false;
  auto r = LabReadiness::aggregate( "lab15_data_inspection", slice, passport, inspector, sci, true );
  REQUIRE( r.level == ReadinessLevel::ReadyWithWarnings );
}

TEST_CASE( "4 autonomy allow / deny / downgrade", "[teaching][autonomy]" )
{
  auto policy = parse( R"({
    "schema": "sicnu.autonomy-policy/1",
    "level": "L5",
    "mode": "exam"
  })" );
  auto d = AutonomyEffectiveDisplay::fromPolicyDoc( policy, "student", "lab" );
  REQUIRE( d.ok );
  // exam ceiling → L2
  REQUIRE( d.effectiveLevel == "L2" );

  using namespace sicnu::agent::autonomy::assistance_capabilities;
  auto allow = d.requestCapability( policy, kConceptHint, "student", "lab" );
  REQUIRE( allow["decision"].asString() == "allow" );

  auto deny = d.requestCapability( policy, kAutonomousExecution, "student", "lab" );
  REQUIRE( ( deny["decision"].asString() == "deny"
             || deny["decision"].asString() == "forbidden" ) );

  auto down = d.requestCapability( policy, kNextStepRecommendation, "student", "lab" );
  // exam L2: next-step is above → downgrade or deny
  REQUIRE( ( down["decision"].asString() == "downgrade"
             || down["decision"].asString() == "deny"
             || down["decision"].asString() == "limited" ) );
}

TEST_CASE( "5 human-only step flagged", "[teaching][timeline][human]" )
{
  auto lab = parse( R"({
    "id": "lab_x",
    "title": "X",
    "steps": [
      {"title": "Click UI", "action": "showProcessingToolbox"},
      {"title": "Run", "operator_id": "rs:ndvi", "params": {}}
    ]
  })" );
  auto tl = LabStepTimeline::fromLabSpec( lab );
  REQUIRE( tl.ok );
  REQUIRE( tl.steps[0].humanRequired );
  REQUIRE( tl.steps[0].kind == "ui_action" );
  REQUIRE_FALSE( tl.steps[1].humanRequired );
}

TEST_CASE( "6 unknown operator blocks readiness", "[teaching][readiness][operator]" )
{
  auto slice = parse( R"({
    "lab_id": "lab15_data_inspection",
    "resolvable": "labspec",
    "operators": [{"operator_id": "rs:does_not_exist", "state": "unknown"}],
    "data_packs": [{"name": "lab15_data_inspection", "present": true}]
  })" );
  Json::Value passport( Json::objectValue );
  passport["ok"] = true;
  Json::Value inspector( Json::objectValue );
  inspector["blocking"] = false;
  Json::Value sci( Json::objectValue );
  sci["conflict"] = false;
  auto r = LabReadiness::aggregate( "lab15_data_inspection", slice, passport, inspector, sci, true );
  REQUIRE( r.level == ReadinessLevel::Blocked );
}

TEST_CASE( "7 missing dataset blocks readiness", "[teaching][readiness][pack]" )
{
  auto slice = parse( R"({
    "lab_id": "lab15_data_inspection",
    "resolvable": "labspec",
    "operators": [{"operator_id": "rs:extract_bands", "state": "available"}],
    "data_packs": [{"name": "lab15_data_inspection", "present": false}]
  })" );
  Json::Value passport( Json::objectValue );
  passport["ok"] = true;
  Json::Value inspector( Json::objectValue );
  inspector["blocking"] = false;
  Json::Value sci( Json::objectValue );
  sci["conflict"] = false;
  auto r = LabReadiness::aggregate( "lab15_data_inspection", slice, passport, inspector, sci, true );
  REQUIRE( r.level == ReadinessLevel::Blocked );
}

TEST_CASE( "8 scientific conflict blocks readiness", "[teaching][readiness][sci]" )
{
  auto slice = parse( R"({
    "lab_id": "lab15_data_inspection",
    "resolvable": "labspec",
    "operators": [{"operator_id": "rs:extract_bands", "state": "available"}],
    "data_packs": [{"name": "lab15_data_inspection", "present": true}]
  })" );
  Json::Value passport( Json::objectValue );
  passport["ok"] = true;
  Json::Value inspector( Json::objectValue );
  inspector["blocking"] = false;
  Json::Value sci( Json::objectValue );
  sci["conflict"] = true;
  sci["reason_zh"] = "CRS 与目标产品冲突";
  auto r = LabReadiness::aggregate( "lab15_data_inspection", slice, passport, inspector, sci, true );
  REQUIRE( r.level == ReadinessLevel::Blocked );
}

TEST_CASE( "9 verifier indeterminate MUST NOT count as pass", "[teaching][feedback]" )
{
  auto verifier = parse( R"({
    "status": "indeterminate",
    "reason_zh": "缺少对照产物",
    "checks": [
      {"id": "c1", "status": "indeterminate", "reason_zh": "无法核对"}
    ]
  })" );
  auto fb = LabFeedbackProjection::fromReports( "lab15", verifier, Json::Value() );
  REQUIRE( fb.ok );
  REQUIRE( fb.overallStatus == "indeterminate" );
  REQUIRE_FALSE( fb.overallCountsAsPass );
  for ( const auto &row : fb.rows ) {
    if ( row.status == "indeterminate" ) REQUIRE_FALSE( row.countsAsPass );
  }
  // Empty reports → indeterminate, not pass
  auto empty = LabFeedbackProjection::fromReports( "lab15", Json::Value(), Json::Value() );
  REQUIRE( empty.overallStatus == "indeterminate" );
  REQUIRE_FALSE( empty.overallCountsAsPass );
}

TEST_CASE( "10 grader feedback projection without leaking answers", "[teaching][feedback][grader]" )
{
  auto grader = parse( R"({
    "schema": "sicnu.grader.report/1",
    "overall": "fail",
    "summary_zh": "过程分不足",
    "earned_points": 2,
    "max_points": 5,
    "criteria": [
      {
        "id": "c_stage",
        "outcome": "not_earned",
        "reasons": ["grader:missing_stage"],
        "evidence_ids": ["ev1"]
      }
    ]
  })" );
  auto fb = LabFeedbackProjection::fromReports( "lab15", Json::Value(), grader );
  REQUIRE( fb.ok );
  REQUIRE( fb.overallStatus == "fail" );
  REQUIRE_FALSE( fb.overallCountsAsPass );
  bool foundSlug = false;
  for ( const auto &row : fb.rows ) {
    if ( row.reasonZh.find( "grader:missing_stage" ) != std::string::npos ) foundSlug = true;
    // Must not embed a golden numeric answer field
    REQUIRE( row.reasonZh.find( "expected_answer" ) == std::string::npos );
  }
  REQUIRE( foundSlug );
  REQUIRE( fb.graderScore.isObject() );
}

TEST_CASE( "11 session save and reload", "[teaching][session]" )
{
  auto s = LabSessionState::makeNew( "local/lab15", "undergraduate_rs", "lab15_data_inspection" );
  REQUIRE( s.ok );
  s.stepIndex = 2;
  s.evidenceRefs.push_back( "human:step_1:12" );
  s.capsuleExportRef = "capsule:pending/lab15";
  const auto bytes = s.serialize();
  auto loaded = LabSessionState::deserialize( bytes );
  REQUIRE( loaded.ok );
  REQUIRE( loaded.labId == "lab15_data_inspection" );
  REQUIRE( loaded.stepIndex == 2 );
  REQUIRE( loaded.evidenceRefs.size() == 1 );
  REQUIRE( loaded.capsuleExportRef == "capsule:pending/lab15" );

  const auto dir = fs::temp_directory_path() / "sicnu_teaching_session_test";
  fs::create_directories( dir );
  const auto path = ( dir / "session.json" ).string();
  REQUIRE( s.saveToFile( path ) );
  auto fromFile = LabSessionState::loadFromFile( path );
  REQUIRE( fromFile.ok );
  REQUIRE( fromFile.sessionId == s.sessionId );
}

TEST_CASE( "12 corrupted session fail-closed", "[teaching][session][failclosed]" )
{
  auto badSchema = LabSessionState::deserialize( R"({"schema":"nope","session_id":"a","course_id":"c","lab_id":"l"})" );
  REQUIRE_FALSE( badSchema.ok );

  auto unknownKey = LabSessionState::deserialize(
    R"({"schema":"sicnu.teaching.session/1","session_id":"a","course_id":"c","lab_id":"l","evil":true})" );
  REQUIRE_FALSE( unknownKey.ok );

  auto badJson = LabSessionState::deserialize( "{not json" );
  REQUIRE_FALSE( badJson.ok );

  auto missing = LabSessionState::loadFromFile( "/tmp/sicnu_no_such_session_file_42.json" );
  REQUIRE_FALSE( missing.ok );
}

TEST_CASE( "13 offline mode readiness item", "[teaching][offline]" )
{
  auto slice = parse( R"({
    "lab_id": "lab15_data_inspection",
    "resolvable": "labspec",
    "operators": [{"operator_id": "rs:extract_bands", "state": "available"}],
    "data_packs": [{"name": "lab15_data_inspection", "present": true}]
  })" );
  Json::Value passport( Json::objectValue );
  passport["ok"] = true;
  Json::Value inspector( Json::objectValue );
  inspector["blocking"] = false;
  Json::Value sci( Json::objectValue );
  sci["conflict"] = false;
  auto r = LabReadiness::aggregate( "lab15_data_inspection", slice, passport, inspector, sci, true );
  bool foundOffline = false;
  for ( const auto &it : r.items )
    if ( it.category == "offline" ) foundOffline = true;
  REQUIRE( foundOffline );
  REQUIRE( r.offlineCapable );
  REQUIRE( r.level == ReadinessLevel::Ready );
}

TEST_CASE( "15 beginner and expert share same truth sources", "[teaching][mode]" )
{
  Json::Value progress( Json::objectValue );
  progress["schema"] = "sicnu.curriculum.progress.summary/1";
  progress["overall_percent"] = 0;
  progress["completed_lab_ids"] = Json::Value( Json::arrayValue );
  auto b = CourseHomeViewModel::fromDocuments( minimalManifest(), progress, availabilityOk(),
                                               ExperienceMode::Beginner );
  auto e = CourseHomeViewModel::fromDocuments( minimalManifest(), progress, availabilityOk(),
                                               ExperienceMode::Expert );
  REQUIRE( b.ok );
  REQUIRE( e.ok );
  REQUIRE( b.modules.size() == e.modules.size() );
  REQUIRE( b.modules[0].labs[0].status == e.modules[0].labs[0].status );
  REQUIRE( b.modules[1].labs[0].status == e.modules[1].labs[0].status );
  REQUIRE( b.continueLabId == e.continueLabId );
  REQUIRE( b.mode != e.mode );
}

TEST_CASE( "teaching mask hides student-decision params", "[teaching][timeline][mask]" )
{
  auto lab = parse( R"({
    "id": "lab_x",
    "title": "X",
    "steps": [{"title": "t", "operator_id": "rs:ndvi",
               "params": {"NIR": 5, "RED": 4, "expr": "fixed"}}]
  })" );
  auto tl = LabStepTimeline::fromLabSpec( lab );
  tl.applyTeachingMask( {"NIR", "RED"} );
  REQUIRE( tl.steps[0].paramsStudentDecision );
  REQUIRE( tl.steps[0].paramsDisplay["NIR"].asString() == "***" );
  REQUIRE( tl.steps[0].params["NIR"].asInt() == 5 ); // raw kept for prefill
}

#ifndef SICNU_TEST_SOURCE_DIR
#define SICNU_TEST_SOURCE_DIR "."
#endif

TEST_CASE( "real-repo lab15 chain projects end-to-end", "[teaching][e2e][lab15]" )
{
  const fs::path root = fs::path( SICNU_TEST_SOURCE_DIR );
  const fs::path curriculum = root / "data/curriculum/undergraduate_rs.curriculum.json";
  const fs::path lab = root / "data/labs/lab15_data_inspection.lab.json";
  if ( !fs::exists( curriculum ) || !fs::exists( lab ) ) {
    WARN( "repo data missing; skip real-repo chain" );
    return;
  }
  std::ifstream cf( curriculum );
  std::stringstream cs;
  cs << cf.rdbuf();
  auto manifest = parse( cs.str() );
  std::ifstream lf( lab );
  std::stringstream ls;
  ls << lf.rdbuf();
  auto labDoc = parse( ls.str() );

  Json::Value progress( Json::objectValue );
  progress["schema"] = "sicnu.curriculum.progress.summary/1";
  progress["overall_percent"] = 0;
  progress["completed_lab_ids"] = Json::Value( Json::arrayValue );

  // Minimal availability for lab15 from its own steps' operators when present.
  Json::Value avail( Json::objectValue );
  avail["schema"] = "sicnu.curriculum.availability/1";
  Json::Value labSlice( Json::objectValue );
  labSlice["lab_id"] = "lab15_data_inspection";
  labSlice["resolvable"] = "labspec";
  labSlice["operators"] = Json::Value( Json::arrayValue );
  labSlice["data_packs"] = Json::Value( Json::arrayValue );
  Json::Value pack( Json::objectValue );
  pack["name"] = "lab15_data_inspection";
  pack["present"] = true;
  labSlice["data_packs"].append( pack );
  Json::Value mod( Json::objectValue );
  mod["module_id"] = "m01_rs_data_basics";
  mod["labs"] = Json::Value( Json::arrayValue );
  mod["labs"].append( labSlice );
  avail["modules"] = Json::Value( Json::arrayValue );
  avail["modules"].append( mod );

  auto vm = CourseHomeViewModel::fromDocuments( manifest, progress, avail );
  REQUIRE( vm.ok );
  REQUIRE( vm.courseId == "undergraduate_rs" );

  auto tl = LabStepTimeline::fromLabDocument( labDoc );
  REQUIRE( tl.ok );
  REQUIRE( tl.labId == "lab15_data_inspection" );
  REQUIRE( tl.steps.size() >= 1 );

  auto readiness = LabReadiness::aggregate(
    "lab15_data_inspection", labSlice,
    parse( R"({"ok":true})" ), parse( R"({"blocking":false})" ),
    parse( R"({"conflict":false})" ), true );
  REQUIRE( readiness.level == ReadinessLevel::Ready );

  auto session = LabSessionState::makeNew( "e2e/lab15", vm.courseId, "lab15_data_inspection" );
  REQUIRE( session.ok );
  session.stepIndex = 0;
  auto fb = LabFeedbackProjection::fromReports( "lab15_data_inspection", Json::Value(),
                                                Json::Value(), "capsule:hook/lab15" );
  REQUIRE_FALSE( fb.overallCountsAsPass );
  session.lastValidationSummary = fb.toJson();
  session.capsuleExportRef = "capsule:hook/lab15";
  const auto roundtrip = LabSessionState::deserialize( session.serialize() );
  REQUIRE( roundtrip.ok );
  REQUIRE( roundtrip.capsuleExportRef == "capsule:hook/lab15" );
}

// ── R1 (G9): the validate flow must project the REAL grade document ──────
// `OutputVerifier::LabGradeResult::toBodyJson()` is the canonical body of a
// `sicnu.lab.grade/1` transcript (the same bytes `lab --grade` records).
// The projection must understand it verbatim: per-assertion rows from
// evidence[], deduction messages as reasons, unverifiable → indeterminate
// with the engine error surfaced — and expected/observed goldens NEVER leak.

TEST_CASE( "16 grade transcript body projects per-assertion rows", "[teaching][feedback][grade]" )
{
  const auto body = parse( R"({
    "lab_id": "lab12_sar_processing",
    "artifact": "/tmp/out.tif",
    "rules": "data/labs/grading/sar_processing.rules.json",
    "verdict": "fail",
    "score": 40.0,
    "passing_score": 60.0,
    "capped_by_blocking": true,
    "deductions": [
      { "assertion_id": "crs_match", "kind": "identity", "severity": "blocking",
        "weight": 40.0, "observed": null, "expected": "EPSG:32650",
        "delta": null, "message": "CRS 与要求不一致" }
    ],
    "evidence": [
      { "assertion_id": "crs_match", "kind": "identity", "passed": false,
        "observed": null, "expected": "EPSG:32650" },
      { "assertion_id": "grid_cells", "kind": "grid", "passed": true,
        "observed": 900, "expected": 900 }
    ],
    "summary": {}
  })" );

  auto fb = LabFeedbackProjection::fromReports( "lab12_sar_processing", Json::Value(), body );
  REQUIRE( fb.ok );
  REQUIRE( fb.overallStatus == "fail" );
  REQUIRE_FALSE( fb.overallCountsAsPass );

  // Per-assertion rows projected from evidence[] with engine ids.
  bool sawCrsRow = false, sawGridRow = false;
  for ( const auto &row : fb.rows ) {
    REQUIRE( row.layer == "grader" );
    if ( row.id == "crs_match" ) {
      sawCrsRow = true;
      REQUIRE( row.status == "fail" );
      REQUIRE_FALSE( row.countsAsPass );
      REQUIRE( row.reasonZh.find( "CRS 与要求不一致" ) != std::string::npos );
    }
    if ( row.id == "grid_cells" ) {
      sawGridRow = true;
      REQUIRE( row.status == "pass" );
      REQUIRE( row.countsAsPass );
    }
  }
  REQUIRE( sawCrsRow );
  REQUIRE( sawGridRow );

  // Golden expected values never leak into the student surface.
  for ( const auto &row : fb.rows ) {
    REQUIRE( row.reasonZh.find( "EPSG:32650" ) == std::string::npos );
  }
  const auto asJson = fb.toJson();
  const auto rowsJson = asJson["rows"];
  REQUIRE( rowsJson.isArray() );
  for ( const auto &row : rowsJson ) {
    REQUIRE( row["reason_zh"].asString().find( "32650" ) == std::string::npos );
  }
}

TEST_CASE( "17 grade transcript score projects with pass line", "[teaching][feedback][grade]" )
{
  const auto body = parse( R"({
    "lab_id": "lab16_accuracy_assessment",
    "verdict": "pass",
    "score": 92.5,
    "passing_score": 60.0,
    "capped_by_blocking": false,
    "deductions": [],
    "evidence": [
      { "assertion_id": "kappa_min", "kind": "accuracy", "passed": true,
        "observed": 0.81, "expected": 0.7 }
    ]
  })" );

  // The real validate flow runs BOTH lenses; the fail-closed aggregate keeps
  // a missing lens indeterminate, so provide a passing verifier report too.
  const auto verifier = parse( R"({"status": "pass", "reason_zh": "结构完整"})" );
  auto fb = LabFeedbackProjection::fromReports( "lab16_accuracy_assessment", verifier, body );
  REQUIRE( fb.ok );
  REQUIRE( fb.overallStatus == "pass" );
  REQUIRE( fb.overallCountsAsPass );
  REQUIRE( fb.graderScore.isObject() );
  REQUIRE( fb.graderScore["score"].asDouble() == 92.5 );
  REQUIRE( fb.graderScore["passing_score"].asDouble() == 60.0 );

  // Missing verifier lens stays indeterminate even when the grader passed.
  auto graderOnly = LabFeedbackProjection::fromReports(
    "lab16_accuracy_assessment", Json::Value(), body );
  REQUIRE( graderOnly.overallStatus == "indeterminate" );
  REQUIRE_FALSE( graderOnly.overallCountsAsPass );
}

TEST_CASE( "18 unverifiable grade stays indeterminate and surfaces the error",
           "[teaching][feedback][grade]" )
{
  // What gradeForTeaching returns when rules/artifact cannot be graded —
  // the engine refusal, NOT a pass.
  const auto body = parse( R"({
    "lab_id": "lab01_image_enhancement",
    "verdict": "unverifiable",
    "score": 0.0,
    "passing_score": 60.0,
    "capped_by_blocking": false,
    "deductions": [],
    "evidence": [],
    "error": "no grading rules found for lab01_image_enhancement"
  })" );

  auto fb = LabFeedbackProjection::fromReports( "lab01_image_enhancement", Json::Value(), body );
  REQUIRE( fb.ok );
  REQUIRE( fb.overallStatus == "indeterminate" );
  REQUIRE_FALSE( fb.overallCountsAsPass );
  bool sawError = false;
  for ( const auto &i : fb.issuesZh ) {
    if ( i.find( "no grading rules found" ) != std::string::npos ) sawError = true;
  }
  REQUIRE( sawError );
  bool sawOverallRow = false;
  for ( const auto &row : fb.rows ) {
    if ( row.id == "grader.overall" ) {
      sawOverallRow = true;
      REQUIRE( row.status == "indeterminate" );
      REQUIRE_FALSE( row.countsAsPass );
    }
  }
  REQUIRE( sawOverallRow );
}

// ── R6 (G7): session carries the validated artifact ref, and a persisted
// feedback summary can be re-rendered after restart — without recomputing.

TEST_CASE( "19 session artifact path round-trips and stays optional",
           "[teaching][session][artifact]" )
{
  auto s = LabSessionState::makeNew( "local/lab12", "undergraduate_rs", "lab12_sar_processing" );
  REQUIRE( s.ok );
  s.artifactPath = "/home/student/lab12/out.tif";
  const auto rt = LabSessionState::deserialize( s.serialize() );
  REQUIRE( rt.ok );
  REQUIRE( rt.artifactPath == "/home/student/lab12/out.tif" );

  // Old documents without the key still parse (optional field).
  const auto legacy = parse( R"({
    "schema": "sicnu.teaching.session/1",
    "session_id": "old/1", "course_id": "undergraduate_rs", "lab_id": "lab15"
  })" );
  auto parsed = LabSessionState::fromJson( legacy );
  REQUIRE( parsed.ok );
  REQUIRE( parsed.artifactPath.empty() );

  // Wrong type is refused, not coerced.
  const auto bad = parse( R"({
    "schema": "sicnu.teaching.session/1",
    "session_id": "old/1", "course_id": "undergraduate_rs", "lab_id": "lab15",
    "artifact_path": 42
  })" );
  auto refused = LabSessionState::fromJson( bad );
  REQUIRE_FALSE( refused.ok );
}

TEST_CASE( "20 persisted feedback summary re-renders without recomputation",
           "[teaching][feedback][restart]" )
{
  const auto body = parse( R"({
    "lab_id": "lab12_sar_processing",
    "verdict": "fail",
    "score": 40.0,
    "passing_score": 60.0,
    "deductions": [ { "assertion_id": "crs_match", "kind": "identity",
                      "severity": "blocking", "weight": 40.0,
                      "message": "CRS 与要求不一致" } ],
    "evidence": [ { "assertion_id": "crs_match", "kind": "identity",
                    "passed": false } ]
  })" );
  const auto original = LabFeedbackProjection::fromReports(
    "lab12_sar_processing", Json::Value(), body );
  REQUIRE( original.overallStatus == "fail" );

  // Restart path: the serialized summary comes back and must keep its
  // verdict, rows and fail-closed invariants byte-identically.
  const auto doc = original.toJson();
  auto restored = LabFeedbackProjection::fromJson( doc );
  REQUIRE( restored.ok );
  REQUIRE( restored.labId == original.labId );
  REQUIRE( restored.overallStatus == original.overallStatus );
  REQUIRE( restored.overallCountsAsPass == original.overallCountsAsPass );
  REQUIRE( restored.rows.size() == original.rows.size() );
  for ( const auto &row : restored.rows ) {
    if ( row.status == "indeterminate" ) REQUIRE_FALSE( row.countsAsPass );
  }

  // Garbage never becomes feedback.
  auto garbage = LabFeedbackProjection::fromJson( parse( R"({"schema": "other"})" ) );
  REQUIRE_FALSE( garbage.ok );
  REQUIRE( garbage.overallStatus == "indeterminate" );
  REQUIRE_FALSE( garbage.overallCountsAsPass );
}

TEST_CASE( "21 readiness stays honest when no preflight facts are injected",
           "[teaching][readiness][honest]" )
{
  // The dock previously fabricated passport=ok / inspector=clean /
  // sci=clean. With nothing injected the aggregate must show UNKNOWN, not
  // Ready (fail-closed contract the dock now relies on).
  const auto slice = parse( R"({
    "lab_id": "lab15", "resolvable": "labspec",
    "operators": [ {"operator_id": "io:inspect", "state": "registered"} ],
    "data_packs": []
  })" );
  auto r = LabReadiness::aggregate( "lab15", slice, Json::Value(),
                                    Json::Value(), Json::Value(), true );
  REQUIRE( r.level == ReadinessLevel::Unknown );
  bool sawPassportUnknown = false, sawInspectorUnknown = false;
  for ( const auto &it : r.items ) {
    if ( it.id == "passport.missing" && it.severity == "unknown" ) sawPassportUnknown = true;
    if ( it.id == "inspector.missing" && it.severity == "unknown" ) sawInspectorUnknown = true;
  }
  REQUIRE( sawPassportUnknown );
  REQUIRE( sawInspectorUnknown );
}

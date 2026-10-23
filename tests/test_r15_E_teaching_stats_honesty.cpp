// test_r15_E_teaching_stats_honesty.cpp — R15-E / E08 oracle: 评分与进度统计真实.
//
// RESTORED by R20-05 (merge-loss recovery): this file, and the production
// fixes it pins, were dropped wholesale by merge c8f5e0805422 (r16-b merging
// PR #1517's r15-E side) and never came back — R19-02's CT-1 audit flagged
// the loss; R20-05 verified `course_home_view_model.cpp` and
// `teaching_admin_dock.cpp` were still byte-identical to the merge base.
//
// Original reproduction on master 46791bc65c (re-confirmed RED on master
// 28e1891390 before the R20-05 restoration):
//
//  1. CourseHomeViewModel copied progressSummary["overall_percent"] verbatim —
//     a corrupt "105" rendered as "Overall progress: 105%", and a stale "0"
//     was shown while the completed_lab_ids already listed finished labs.
//
//  2. LabFeedbackProjection graded an evidence entry that carried no boolean
//     "passed" field as "fail". Aggregate() then folded the whole lab to
//     "fail", so an assertion the engine never evaluated failed the student —
//     while the same file's normalizeStatus() folds every unknown word to
//     "indeterminate" precisely so it can never read as a pass OR a fail.
#include "teaching/course_home_view_model.h"
#include "teaching/lab_feedback_projection.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

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

Json::Value minimalManifest()
{
    return parse( R"({
    "schema": "sicnu.curriculum/1",
    "id": "undergraduate_rs",
    "title_zh": "测试课程",
    "audience_zh": "测试听众",
    "modules": [
      {
        "id": "m01", "index": 1, "title_zh": "基础", "summary_zh": "s",
        "learning_outcomes": ["能读元数据"], "prerequisite_modules": [],
        "estimated_effort_minutes": 60, "optional": false,
        "labs": [
          { "lab_id": "lab15_data_inspection", "role": "core",
            "estimated_effort_minutes": 60,
            "required_data_packs": ["lab15_data_inspection"] }
        ]
      },
      {
        "id": "m02", "index": 2, "title_zh": "进阶", "summary_zh": "s2",
        "learning_outcomes": ["能做指数"], "prerequisite_modules": ["m01"],
        "estimated_effort_minutes": 90, "optional": false,
        "labs": [
          { "lab_id": "lab02_spectral_analysis", "role": "core",
            "estimated_effort_minutes": 90,
            "required_data_packs": ["lab02_spectral_analysis"] }
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
      { "module_id": "m01", "labs": [
        { "lab_id": "lab15_data_inspection", "resolvable": "labspec",
          "operators": [{"operator_id": "rs:extract_bands", "state": "available"}],
          "data_packs": [{"name": "lab15_data_inspection", "present": true}] } ] },
      { "module_id": "m02", "labs": [
        { "lab_id": "lab02_spectral_analysis", "resolvable": "labspec",
          "operators": [{"operator_id": "rs:ndvi", "state": "available"}],
          "data_packs": [{"name": "lab02_spectral_analysis", "present": true}] } ] }
    ]
  })" );
}

Json::Value progressWith( int percent )
{
    Json::Value p( Json::objectValue );
    p["schema"] = "sicnu.curriculum.progress.summary/1";
    p["overall_percent"] = percent;
    p["completed_lab_ids"] = Json::Value( Json::arrayValue );
    return p;
}

/// A curriculum with no labs at all: the projection has nothing to divide by,
/// so the (clamped) upstream hint is what gets shown.
Json::Value zeroLabManifest()
{
    return parse( R"({
    "schema": "sicnu.curriculum/1",
    "id": "empty_course",
    "title_zh": "空课程",
    "audience_zh": "测试听众",
    "modules": []
  })" );
}

/// The same lab listed by two modules is ONE lab — the denominator must not
/// double-count it (the upstream progress summary keys on lab ids too).
Json::Value duplicatedLabManifest()
{
    return parse( R"({
    "schema": "sicnu.curriculum/1",
    "id": "dup_course",
    "title_zh": "重复课程",
    "audience_zh": "测试听众",
    "modules": [
      { "id": "m01", "index": 1, "title_zh": "A", "summary_zh": "s",
        "learning_outcomes": [], "prerequisite_modules": [],
        "estimated_effort_minutes": 60, "optional": false,
        "labs": [ { "lab_id": "lab15_data_inspection", "role": "core",
                    "estimated_effort_minutes": 60, "required_data_packs": [] } ] },
      { "id": "m02", "index": 2, "title_zh": "B", "summary_zh": "s",
        "learning_outcomes": [], "prerequisite_modules": [],
        "estimated_effort_minutes": 60, "optional": true,
        "labs": [ { "lab_id": "lab15_data_inspection", "role": "optional",
                    "estimated_effort_minutes": 60, "required_data_packs": [] } ] }
    ]
  })" );
}

} // namespace

TEST_CASE( "CourseHomeViewModel clamps the reported hint when there is nothing to derive",
           "[r15e][e08][course-home]" )
{
    // No labs → no local derivation → the reported value is used, but clamped.
    auto vm = CourseHomeViewModel::fromDocuments( zeroLabManifest(), progressWith( 105 ),
                                                  availabilityOk() );
    REQUIRE( vm.modules.empty() );
    CHECK( vm.overallPercent == 100 );

    auto vm2 = CourseHomeViewModel::fromDocuments( zeroLabManifest(), progressWith( -7 ),
                                                   availabilityOk() );
    CHECK( vm2.overallPercent == 0 );
}

TEST_CASE( "CourseHomeViewModel counts a duplicated lab once",
           "[r15e][e08][course-home]" )
{
    Json::Value progress = progressWith( 0 );
    progress["completed_lab_ids"].append( "lab15_data_inspection" );
    auto vm = CourseHomeViewModel::fromDocuments( duplicatedLabManifest(), progress,
                                                  availabilityOk() );
    // 1 lab, completed once → 100%, not 1/2 = 50%.
    CHECK( vm.overallPercent == 100 );
}

TEST_CASE( "CourseHomeViewModel clamps an out-of-range overall_percent",
           "[r15e][e08][course-home]" )
{
    auto vm = CourseHomeViewModel::fromDocuments( minimalManifest(), progressWith( 105 ),
                                                  availabilityOk() );
    REQUIRE( vm.ok );
    REQUIRE( vm.overallPercent >= 0 );
    REQUIRE( vm.overallPercent <= 100 );
    REQUIRE( vm.overallPercent == 0 ); // 0 of 2 labs are completed
}

TEST_CASE( "CourseHomeViewModel derives progress from completed labs, not a stale hint",
           "[r15e][e08][course-home]" )
{
    Json::Value progress = progressWith( 0 ); // stale hint: says 0%
    progress["completed_lab_ids"].append( "lab15_data_inspection" );
    progress["completed_lab_ids"].append( "lab02_spectral_analysis" );

    auto vm = CourseHomeViewModel::fromDocuments( minimalManifest(), progress,
                                                  availabilityOk() );
    REQUIRE( vm.ok );
    REQUIRE( vm.overallPercent == 100 ); // 2 of 2 — not the reported 0

    Json::Value half = progressWith( 0 );
    half["completed_lab_ids"].append( "lab15_data_inspection" );
    auto vm2 = CourseHomeViewModel::fromDocuments( minimalManifest(), half,
                                                   availabilityOk() );
    REQUIRE( vm2.overallPercent == 50 ); // 1 of 2
}

TEST_CASE( "LabFeedbackProjection keeps an unevaluated assertion indeterminate",
           "[r15e][e08][feedback]" )
{
    Json::Value grader( Json::objectValue );
    grader["status"] = "passed";
    Json::Value evidence( Json::arrayValue );
    Json::Value e( Json::objectValue );
    e["assertion_id"] = "a1"; // no "passed" member: never evaluated
    evidence.append( e );
    grader["evidence"] = evidence;

    auto p = LabFeedbackProjection::fromReports( "lab15_data_inspection",
                                                 Json::Value( Json::nullValue ), grader );
    bool found = false;
    for ( const auto &r : p.rows )
    {
        if ( r.id == "a1" )
        {
            found = true;
            REQUIRE( r.status == "indeterminate" );
            REQUIRE( r.statusZh == "不确定" );
            REQUIRE( !r.countsAsPass );
        }
    }
    REQUIRE( found );
    // The lab must not be failed by an assertion nobody evaluated.
    REQUIRE( p.overallStatus == "indeterminate" );
    REQUIRE( p.overallStatus != "fail" );
    REQUIRE( !p.overallCountsAsPass );
}

TEST_CASE( "LabFeedbackProjection still fails an assertion that explicitly failed",
           "[r15e][e08][feedback]" )
{
    Json::Value grader( Json::objectValue );
    grader["status"] = "passed";
    Json::Value evidence( Json::arrayValue );
    Json::Value e( Json::objectValue );
    e["assertion_id"] = "a1";
    e["passed"] = false; // explicit engine verdict
    evidence.append( e );
    grader["evidence"] = evidence;

    auto p = LabFeedbackProjection::fromReports( "lab15_data_inspection",
                                                 Json::Value( Json::nullValue ), grader );
    bool found = false;
    for ( const auto &r : p.rows )
    {
        if ( r.id == "a1" )
        {
            found = true;
            REQUIRE( r.status == "fail" );
        }
    }
    REQUIRE( found );
    REQUIRE( p.overallStatus == "fail" );
}

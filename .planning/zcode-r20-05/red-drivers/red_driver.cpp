// R20-05 RED driver: the restored test_r15_E_teaching_stats_honesty assertions
// compiled standalone against the BASE tree (master 28e1891390).
#include "teaching/course_home_view_model.h"
#include "teaching/lab_feedback_projection.h"
#include <json/json.h>
#include <iostream>
#include <sstream>
#include <string>

using namespace sicnu::teaching;
static int failures = 0;
#define CHECK_RED( expr ) do { if ( !( expr ) ) { std::cout << "ASSERTION FAILS on base (=RED evidence): " #expr "\n"; } else { std::cout << "ASSERTION HOLDS on base (no red): " #expr "\n"; ++failures; } } while ( 0 )

static Json::Value parse( const std::string &text )
{
    Json::Value root; Json::CharReaderBuilder b; std::string errs; std::istringstream ss( text );
    if ( !Json::parseFromStream( b, ss, &root, &errs ) ) abort();
    return root;
}
int main()
{
    Json::Value manifest = parse( R"({
    "schema": "sicnu.curriculum/1", "id": "c", "title_zh": "t", "audience_zh": "a",
    "modules": [
      { "id": "m01", "index": 1, "title_zh": "b", "summary_zh": "s", "learning_outcomes": [],
        "prerequisite_modules": [], "estimated_effort_minutes": 60, "optional": false,
        "labs": [ { "lab_id": "lab15_data_inspection", "role": "core", "estimated_effort_minutes": 60, "required_data_packs": [] } ] },
      { "id": "m02", "index": 2, "title_zh": "i", "summary_zh": "s", "learning_outcomes": [],
        "prerequisite_modules": ["m01"], "estimated_effort_minutes": 90, "optional": false,
        "labs": [ { "lab_id": "lab02_spectral_analysis", "role": "core", "estimated_effort_minutes": 90, "required_data_packs": [] } ] }
    ] })" );
    Json::Value avail = parse( R"({
    "schema": "sicnu.curriculum.availability/1",
    "modules": [
      { "module_id": "m01", "labs": [ { "lab_id": "lab15_data_inspection", "resolvable": "labspec",
        "operators": [], "data_packs": [] } ] },
      { "module_id": "m02", "labs": [ { "lab_id": "lab02_spectral_analysis", "resolvable": "labspec",
        "operators": [], "data_packs": [] } ] } ] })" );

    // Case 1 (clamp + derive): stale 0 hint, both labs completed.
    Json::Value progress( Json::objectValue );
    progress["schema"] = "sicnu.curriculum.progress.summary/1";
    progress["overall_percent"] = 0;
    progress["completed_lab_ids"] = Json::Value( Json::arrayValue );
    progress["completed_lab_ids"].append( "lab15_data_inspection" );
    progress["completed_lab_ids"].append( "lab02_spectral_analysis" );
    auto vm = CourseHomeViewModel::fromDocuments( manifest, progress, avail );
    CHECK_RED( vm.overallPercent == 100 );   // base renders the stale 0

    // Case 2 (feedback): evidence entry with NO boolean passed member.
    Json::Value grader( Json::objectValue );
    grader["status"] = "passed";
    Json::Value evidence( Json::arrayValue );
    Json::Value e( Json::objectValue );
    e["assertion_id"] = "a1";
    evidence.append( e );
    grader["evidence"] = evidence;
    auto p = LabFeedbackProjection::fromReports( "lab15_data_inspection", Json::Value( Json::nullValue ), grader );
    bool found = false;
    std::string rowStatus;
    for ( const auto &r : p.rows ) if ( r.id == "a1" ) { found = true; rowStatus = r.status; }
    CHECK_RED( found && rowStatus == "indeterminate" );   // base says "fail"
    CHECK_RED( p.overallStatus == "indeterminate" );      // base says "fail"

    std::cout << ( failures > 0 ? "RED-CONFIRMED: all restored assertions fail on base (the lost fixes are really missing)\n"
                                : "NO-RED: base already behaves as restored\n" );
    return failures == 0 ? 0 : 1;  // 0 when every restored assertion fails on this tree (RED)
}

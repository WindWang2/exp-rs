// test_r33_t04_teaching_feedback.cpp — R33-T04 / WP14 (#1953 residual):
//
//   The grader-overall row was pushed into p.rows and the indeterminate
//   error enrichment then mutated the DEAD LOCAL copy — the
//   unavailable-grade row reached the UI with no reason attached. The
//   stored row must carry the typed unavailability reason.
#include "teaching/lab_feedback_projection.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

using namespace sicnu::teaching;

TEST_CASE( "an unavailable grader report carries its reason on the grader.overall row",
           "[teaching][feedback][r33t04]" )
{
    Json::Value grader( Json::objectValue );
    grader["overall"] = "indeterminate";
    grader["error"] = "grader backend unreachable";

    const auto p = LabFeedbackProjection::fromReports( "lab15_data_inspection",
                                                       Json::Value( Json::nullValue ), grader );

    const FeedbackCheckRow *grow = nullptr;
    for ( const auto &r : p.rows )
    {
        if ( r.id == "grader.overall" )
        {
            grow = &r;
            break;
        }
    }
    REQUIRE( grow );
    CHECK( grow->status == "indeterminate" );
    CHECK_FALSE( grow->countsAsPass );
    // The row itself must explain WHY the grade is unavailable — the old
    // code lost this on a dead local copy.
    CHECK( grow->reasonZh.find( "grader backend unreachable" ) != std::string::npos );

    // The typed issue is still surfaced.
    bool issuePresent = false;
    for ( const auto &issue : p.issuesZh )
        if ( issue.find( "grader backend unreachable" ) != std::string::npos )
            issuePresent = true;
    CHECK( issuePresent );
}

TEST_CASE( "an available grader report keeps summary_zh as the overall reason",
           "[teaching][feedback][r33t04]" )
{
    Json::Value grader( Json::objectValue );
    grader["overall"] = "earned";
    grader["summary_zh"] = "All criteria met";

    const auto p = LabFeedbackProjection::fromReports( "lab02_spectral_analysis",
                                                       Json::Value( Json::nullValue ), grader );

    for ( const auto &r : p.rows )
    {
        if ( r.id == "grader.overall" )
        {
            CHECK( r.status == "pass" );
            CHECK( r.countsAsPass );
            CHECK( r.reasonZh == "All criteria met" );
        }
    }
}

// test_r33_t04_teaching_readiness_types.cpp — R33-T04 / WP12 (#1953
// residuals):
//
//   * LabReadiness treated a passport with a present-but-non-bool ok as a
//     PASS (fail-open), and a passport object without ok was silently
//     swallowed. Every shape must land on an explicit outcome: true → pass,
//     false → block, missing/null/string/object → unknown.
//   * CourseHomeViewModel clamped AFTER the int cast —
//     static_cast<int>(1e300) is UB, not a clamp. The bound must be applied
//     in the double domain first.
#include "teaching/course_home_view_model.h"
#include "teaching/lab_readiness.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cmath>
#include <limits>
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

/// Lab-less curriculum: the only shape where the reported overall_percent
/// hint survives (the completed-lab ratio needs a denominator).
Json::Value labLessManifest()
{
    return parse( R"({
    "schema": "sicnu.curriculum/1",
    "id": "empty_course",
    "title_zh": "空课程",
    "modules": [
      { "id": "m01", "index": 1, "title_zh": "基础", "summary_zh": "s",
        "learning_outcomes": [], "prerequisite_modules": [],
        "estimated_effort_minutes": 30, "optional": false, "labs": [] }
    ]
  })" );
}

Json::Value availabilityOk()
{
    return parse( R"({
    "schema": "sicnu.curriculum.availability/1",
    "modules": [
      { "module_id": "m01", "labs": [] }
    ]
  })" );
}

const ReadinessItem *findItem( const LabReadiness &r, const std::string &id )
{
    for ( const auto &it : r.items )
        if ( it.id == id )
            return &it;
    return nullptr;
}
} // namespace

TEST_CASE( "LabReadiness passport ok shapes: true/false/missing/null/string each land on an explicit outcome",
           "[teaching][r33t04]" )
{
    const Json::Value slice;      // not an object → avail.missing (unknown)
    const Json::Value inspector;  // not an object → inspector.missing (unknown)
    const Json::Value sci;        // not an object → scientific unknown
    const bool offline = false;

    SECTION( "ok=true is the only pass" )
    {
        Json::Value passport( Json::objectValue );
        passport["ok"] = true;
        const auto r = LabReadiness::aggregate( "lab_x", slice, passport, inspector, sci, offline );
        const auto *item = findItem( r, "passport.ok" );
        REQUIRE( item );
        CHECK( item->severity == "info" );
        CHECK( item->ok );
    }

    SECTION( "ok=false blocks" )
    {
        Json::Value passport( Json::objectValue );
        passport["ok"] = false;
        const auto r = LabReadiness::aggregate( "lab_x", slice, passport, inspector, sci, offline );
        const auto *item = findItem( r, "passport.fail" );
        REQUIRE( item );
        CHECK( item->severity == "block" );
        CHECK_FALSE( item->ok );
        CHECK( r.level == ReadinessLevel::Blocked );
    }

    SECTION( "ok missing is unknown, not a silent pass" )
    {
        Json::Value passport( Json::objectValue );
        passport["revision"] = "1";
        const auto r = LabReadiness::aggregate( "lab_x", slice, passport, inspector, sci, offline );
        const auto *item = findItem( r, "passport.ok_missing" );
        REQUIRE( item );
        CHECK( item->severity == "unknown" );
        CHECK_FALSE( item->ok );
    }

    SECTION( "ok null is unknown" )
    {
        Json::Value passport( Json::objectValue );
        passport["ok"] = Json::Value();
        const auto r = LabReadiness::aggregate( "lab_x", slice, passport, inspector, sci, offline );
        const auto *item = findItem( r, "passport.ok_type" );
        REQUIRE( item );
        CHECK( item->severity == "unknown" );
        CHECK_FALSE( item->ok );
    }

    SECTION( "ok string/object are unknown — the old fail-open pass" )
    {
        Json::Value withString( Json::objectValue );
        withString["ok"] = "yes";
        const auto r1 = LabReadiness::aggregate( "lab_x", slice, withString, inspector, sci, offline );
        const auto *item1 = findItem( r1, "passport.ok_type" );
        REQUIRE( item1 );
        CHECK( item1->severity == "unknown" );
        CHECK_FALSE( item1->ok ); // previously passport.ok/pass=true (fail-open)

        Json::Value withObject( Json::objectValue );
        withObject["ok"] = Json::Value( Json::objectValue );
        const auto r2 = LabReadiness::aggregate( "lab_x", slice, withObject, inspector, sci, offline );
        const auto *item2 = findItem( r2, "passport.ok_type" );
        REQUIRE( item2 );
        CHECK( item2->severity == "unknown" );
        CHECK_FALSE( item2->ok );

        // And neither shape may register the pass item.
        CHECK_FALSE( findItem( r1, "passport.ok" ) );
        CHECK_FALSE( findItem( r2, "passport.ok" ) );
    }
}

TEST_CASE( "CourseHomeViewModel clamps the percent hint in the double domain before the int cast",
           "[teaching][r33t04]" )
{
    const auto makeProgress = []( double value )
    {
        Json::Value p( Json::objectValue );
        p["schema"] = "sicnu.curriculum.progress.summary/1";
        p["overall_percent"] = value;
        p["completed_lab_ids"] = Json::Value( Json::arrayValue );
        return p;
    };

    SECTION( "1e300 clamps to 100 without UB" )
    {
        const auto vm = CourseHomeViewModel::fromDocuments( labLessManifest(),
                                                            makeProgress( 1e300 ),
                                                            availabilityOk() );
        REQUIRE( vm.ok );
        CHECK( vm.overallPercent == 100 );
    }

    SECTION( "-1e300 clamps to 0" )
    {
        const auto vm = CourseHomeViewModel::fromDocuments( labLessManifest(),
                                                            makeProgress( -1e300 ),
                                                            availabilityOk() );
        REQUIRE( vm.ok );
        CHECK( vm.overallPercent == 0 );
    }

    SECTION( "in-range hint is truncated, boundaries exact" )
    {
        CHECK( CourseHomeViewModel::fromDocuments( labLessManifest(), makeProgress( 99.9 ),
                                                   availabilityOk() )
                   .overallPercent == 99 );
        CHECK( CourseHomeViewModel::fromDocuments( labLessManifest(), makeProgress( 100.0 ),
                                                   availabilityOk() )
                   .overallPercent == 100 );
        CHECK( CourseHomeViewModel::fromDocuments( labLessManifest(), makeProgress( 0.0 ),
                                                   availabilityOk() )
                   .overallPercent == 0 );
    }

    SECTION( "NaN hint is not usable and falls back to 0" )
    {
        const auto vm = CourseHomeViewModel::fromDocuments( labLessManifest(),
                                                            makeProgress(
                                                                std::numeric_limits<double>::quiet_NaN() ),
                                                            availabilityOk() );
        REQUIRE( vm.ok );
        CHECK( vm.overallPercent == 0 );
    }
}

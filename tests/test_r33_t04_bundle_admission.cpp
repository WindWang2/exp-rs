// test_r33_t04_bundle_admission.cpp — R33-T04 / WP18:
//
//   bundleFromJson admitted inconsistent shapes: asset entries were
//   rejected typed, but capability/recipe entries were silently `continue`d
//   (and an OBJECT-shaped collection member was iterated as its member
//   values); a present-but-mistyped planner execution_blocked defaulted to
//   FALSE (silently un-blocking planning on a corrupt recovery document);
//   negative budgets passed through verbatim. Admission is now bounded,
//   type-consistent and fail-closed.
#include <catch2/catch_test_macros.hpp>

#include "science_context/broker.h"
#include "science_context/bundle.h"

#include <json/json.h>

#include <sstream>
#include <string>

using namespace sicnu::science_context;

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

/// A real synthesized bundle serialized to canonical JSON — the legitimate
/// document every corruption case starts from.
std::string legalBundleJson()
{
    ScienceContextBroker broker;
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    const auto result = broker.synthesize( req );
    REQUIRE_FALSE( ( result.bundle.goal.empty() || result.bundle.intent.empty() ) );
    return serializeBundle( result.bundle );
}
} // namespace

TEST_CASE( "bundleFromJson round-trips a real synthesized bundle", "[science_context][r33t04]" )
{
    const auto json = parse( legalBundleJson() );
    ScientificContextBundle bundle;
    std::string error;
    REQUIRE( bundleFromJson( json, bundle, &error ) );
    CHECK( error.empty() );
    CHECK( bundle.goal == "ndvi" );
    CHECK( bundle.schemaId == kBundleSchemaId );
}

TEST_CASE( "collection members must be arrays and entries objects — typed refusal, no silent drops",
           "[science_context][r33t04]" )
{
    const auto base = parse( legalBundleJson() );

    SECTION( "capabilities as object is refused" )
    {
        auto json = base;
        json["capabilities"] = Json::Value( Json::objectValue );
        json["capabilities"]["x"] = Json::Value( Json::objectValue );
        ScientificContextBundle bundle;
        std::string error;
        CHECK_FALSE( bundleFromJson( json, bundle, &error ) );
        CHECK( error.find( "capabilities must be array" ) != std::string::npos );
    }

    SECTION( "a torn capability entry is refused like an asset entry" )
    {
        auto json = base;
        json["capabilities"] = Json::Value( Json::arrayValue );
        json["capabilities"].append( Json::Value( 5 ) );
        ScientificContextBundle bundle;
        std::string error;
        CHECK_FALSE( bundleFromJson( json, bundle, &error ) );
        CHECK( error.find( "capabilities entry must be object" ) != std::string::npos );
    }

    SECTION( "a torn recipe entry is refused" )
    {
        auto json = base;
        json["recipes"] = Json::Value( Json::arrayValue );
        json["recipes"].append( Json::Value( "torn" ) );
        ScientificContextBundle bundle;
        std::string error;
        CHECK_FALSE( bundleFromJson( json, bundle, &error ) );
        CHECK( error.find( "recipes entry must be object" ) != std::string::npos );
    }
}

TEST_CASE( "a mistyped execution_blocked fails closed as blocked", "[science_context][r33t04]" )
{
    const auto base = parse( legalBundleJson() );

    auto json = base;
    json["planner_projection"]["execution_blocked"] = "yes"; // string, not bool
    ScientificContextBundle bundle;
    std::string error;
    REQUIRE( bundleFromJson( json, bundle, &error ) );
    CHECK( bundle.planner.executionBlocked ); // never silently un-blocked
    CHECK( bundle.planner.blockReason.find( "execution_blocked" ) != std::string::npos );

    // The honest shapes keep their meaning.
    json["planner_projection"]["execution_blocked"] = false;
    REQUIRE( bundleFromJson( json, bundle, &error ) );
    CHECK_FALSE( bundle.planner.executionBlocked );
    json["planner_projection"]["execution_blocked"] = true;
    REQUIRE( bundleFromJson( json, bundle, &error ) );
    CHECK( bundle.planner.executionBlocked );
}

TEST_CASE( "negative budgets clamp to zero instead of passing through", "[science_context][r33t04]" )
{
    const auto base = parse( legalBundleJson() );

    auto json = base;
    json["constraints"]["max_bytes"] = -5;
    json["constraints"]["max_recipes"] = -1;
    ScientificContextBundle bundle;
    std::string error;
    REQUIRE( bundleFromJson( json, bundle, &error ) );
    CHECK( bundle.constraints.maxBytes == 0 );
    CHECK( bundle.constraints.maxRecipes == 0 );
}

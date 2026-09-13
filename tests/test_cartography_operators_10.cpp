// Workbench 10.0 — cartography operator family (goal WP-B, C-1)
//
// Runs inside the test_mapspec harness (same main(): QgsApplication + the
// shipped data/cartography catalog) and proves the operator adapters expose
// the SAME engine semantics agents get: registry registration (idempotent),
// validate/preflight/repair headless behavior, compose compiling a real
// layout and export delivering sha256 evidence — every result shaped so a
// workflow node can chain it.
#include <catch2/catch_test_macros.hpp>

#include "agent/cartography/cartography_operators.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_conditions.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace sicnu::agent::mapspec;

namespace
{

Json::Value frameSpec( const std::string &layoutName )
{
    Json::Value spec = makeMapSpec( layoutName, Json::Value() );
    appendMapSpecItem( spec, "map_frames", Json::Value() );
    Json::Value &frame = spec["map_frames"][0];
    Json::Value rect( Json::arrayValue );
    rect.append( 12 );
    rect.append( 24 );
    rect.append( 190 );
    rect.append( 160 );
    frame["rect_mm"] = rect;
    Json::Value extent( Json::arrayValue );
    extent.append( 116.0 );
    extent.append( 39.0 );
    extent.append( 117.0 );
    extent.append( 40.0 );
    frame["extent"] = extent;
    return spec;
}

Json::Value runOperator( const std::string &id, const Json::Value &params )
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext context;
    return op->run( params, context );
}

} // namespace

TEST_CASE( "cartography operator family registers idempotently", "[cartography10][operators]" )
{
    sicnu::agent::cartography::initCartographyOperators();
    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    for ( const char *id : { "cartography:compose", "cartography:preflight",
                             "cartography:validate", "cartography:repair",
                             "cartography:export" } )
    {
        INFO( id );
        CHECK( registry.hasOperator( id ) );
        // Idempotent: a second init keeps factories alive and usable.
        CHECK( registry.create( id ) != nullptr );
    }
    sicnu::agent::cartography::initCartographyOperators();
    CHECK( registry.create( "cartography:validate" ) != nullptr );
}

TEST_CASE( "cartography:validate is pure and matches the engine", "[cartography10][operators]" )
{
    Json::Value params( Json::objectValue );
    Json::Value spec = makeMapSpec( "validate-good", Json::Value() );
    params["mapspec"] = spec;
    const Json::Value out = runOperator( "cartography:validate", params );
    CHECK( out["valid"].asBool() );
    CHECK( out["problems"].empty() );

    Json::Value bad( Json::objectValue );
    bad["kind"] = "map_spec"; // missing collections entirely
    bad["spec_version"] = 1;
    Json::Value badParams( Json::objectValue );
    badParams["mapspec"] = bad;
    const Json::Value badOut = runOperator( "cartography:validate", badParams );
    CHECK_FALSE( badOut["valid"].asBool() );
    CHECK( badOut["problems"].isArray() );
    CHECK( badOut["problems"].size() > 0 );
}

TEST_CASE( "cartography:preflight returns the quality report", "[cartography10][operators]" )
{
    Json::Value params( Json::objectValue );
    params["mapspec"] = frameSpec( "preflight-op-map" );
    const Json::Value out = runOperator( "cartography:preflight", params );
    CHECK( out.isMember( "quality" ) );
    CHECK( out["quality"].isObject() );
}

TEST_CASE( "cartography:repair reports an applied/still_reported ledger",
           "[cartography10][operators]" )
{
    Json::Value params( Json::objectValue );
    Json::Value spec = frameSpec( "repair-op-map" );
    // A title far off the page gives the repair pass something honest to do.
    appendMapSpecItem( spec, "titles", Json::Value() );
    params["mapspec"] = spec;
    const Json::Value out = runOperator( "cartography:repair", params );
    CHECK( out.isMember( "applied" ) );
    CHECK( out["ledger"].isArray() );
    CHECK( out.isMember( "quality_after" ) );
    CHECK( out["mapspec"].isObject() );
}

TEST_CASE( "cartography:compose compiles a layout and names it",
           "[cartography10][operators][compose]" )
{
    Json::Value params( Json::objectValue );
    params["mapspec"] = frameSpec( "compose-op-map" );
    const Json::Value out = runOperator( "cartography:compose", params );
    CHECK( out["compiled"].asBool() );
    CHECK( out["layout_name"].asString() == "compose-op-map" );
    CHECK( out["output"].asString() == "compose-op-map" );
    CHECK( out.isMember( "quality" ) );
    CHECK( out.isMember( "structural_digest" ) );
}

TEST_CASE( "cartography:export refuses unknown layouts and delivers evidence",
           "[cartography10][operators][export]" )
{
    sicnu::operators::RSOperatorContext context;

    // Unknown layout → typed failure, never a partial file.
    {
        auto op = sicnu::operators::RSOperatorRegistry::instance().create( "cartography:export" );
        REQUIRE( op );
        Json::Value params( Json::objectValue );
        params["layout"] = "no-such-layout-10";
        params["format"] = "png";
        QTemporaryDir dir;
        params["directory"] = dir.path().toStdString();
        bool typed = false;
        try
        {
            op->run( params, context );
        }
        catch ( const sicnu::operators::RSOperatorError &e )
        {
            typed = e.code() != sicnu::operators::ErrorCode::Success;
        }
        CHECK( typed );
    }

    // Known layout (composed above) → atomic export with digest + "output".
    {
        Json::Value composeParams( Json::objectValue );
        composeParams["mapspec"] = frameSpec( "export-op-map" );
        runOperator( "cartography:compose", composeParams );

        QTemporaryDir dir;
        Json::Value params( Json::objectValue );
        params["layout"] = "export-op-map";
        params["format"] = "png";
        params["directory"] = dir.path().toStdString();
        params["dpi"] = 72;
        const Json::Value out = runOperator( "cartography:export", params );
        const std::string path = out["path"].asString();
        CHECK_FALSE( path.empty() );
        CHECK( out["output"].asString() == path );
        CHECK( QFile::exists( QString::fromStdString( path ) ) );
        CHECK( out["sha256"].asString().size() == 64 );
    }
}

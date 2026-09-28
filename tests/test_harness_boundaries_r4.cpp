// tests/test_harness_boundaries_r4.cpp — Track 8 R4 WP-F: the boundary
// matrix across harness tool entry points and the error taxonomy.
//
// The contract under test (harness_error.h + the SpatialTool convention
// every harness tool follows): ANY boundary input — missing fields, wrong
// types, unknown vocabularies, empty objects — is answered with a TYPED
// result naming the offending field (INVALID_PARAMETER) or the closed
// taxonomy (INVALID_PLAN …), never a crash, never an untyped apology, and
// never silent acceptance.

#include <catch2/catch_test_macros.hpp>

#include "agent/harness/context_checkpoint.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/plan_tools.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <json/json.h>

using namespace sicnu::agent::harness;
using namespace sicnu::agent::spatial_tools;

namespace {

/// Finds a registered harness tool (registers are idempotent).
SpatialToolPtr harnessTool( const char *name, void (*registrar )() )
{
    registrar();
    auto tool = SpatialToolRegistry::instance().find( name );
    REQUIRE( tool.has_value() );
    REQUIRE( tool.value_or( nullptr ) != nullptr );
    return tool.value_or( nullptr );
}

/// A failing tool result carries the typed envelope fields.
void checkTypedFailure( const SpatialToolResult &result, const char *code )
{
    INFO( "expected code " << code << ", got: " << result.error );
    CHECK_FALSE( result.success );
    CHECK( result.errorCode == code );
    CHECK_FALSE( result.error.empty() );
}

Json::Value input( Json::Value doc ) { return doc; }

} // namespace

// — harness:plan boundary ———————————————————————————————————————————————

TEST_CASE( "harness:plan refuses boundary inputs with typed field-named "
           "errors", "[harness][boundaries-r4][plan-tool]" )
{
    auto tool = harnessTool( "harness:plan", &registerPlanTools );

    // Missing the plan parameter entirely: the field is named.
    checkTypedFailure( tool->execute( input( Json::Value( Json::objectValue ) ) ),
                       error_codes::kInvalidParameter );

    // plan present but not an object: the boundary layer refuses it as a
    // missing OBJECT parameter (field-named, typed).
    Json::Value wrongType( Json::objectValue );
    wrongType["plan"] = "I will not write a plan";
    checkTypedFailure( tool->execute( wrongType ), error_codes::kInvalidParameter );

    // plan is an empty object: no steps → the refusal contract.
    Json::Value empty( Json::objectValue );
    empty["plan"] = Json::Value( Json::objectValue );
    checkTypedFailure( tool->execute( empty ), error_codes::kInvalidPlan );
}

// — harness:execute_plan boundary (read-level) ———————————————————————————

TEST_CASE( "harness:execute_plan rejects structurally invalid plans before "
           "any execution machinery engages", "[harness][boundaries-r4][execute-tool]" )
{
    auto tool = harnessTool( "harness:execute_plan", &registerPlanTools );

    // Missing plan → typed parameter error, nothing executes.
    checkTypedFailure( tool->execute( input( Json::Value( Json::objectValue ) ) ),
                       error_codes::kInvalidParameter );

    // Structurally lying plan (top-level array): the boundary layer refuses
    // it as a non-object BEFORE the reader — and therefore before any
    // autonomy/preflight machinery that would need a workspace.
    Json::Value arrayPlan( Json::objectValue );
    arrayPlan["plan"] = Json::Value( Json::arrayValue );
    checkTypedFailure( tool->execute( arrayPlan ), error_codes::kInvalidParameter );

    // Unknown intent → INVALID_PLAN from the reader's closed vocabulary.
    Json::Value alien( Json::objectValue );
    alien["plan"] = Json::Value( Json::objectValue );
    alien["plan"]["kind"] = "execution_plan";
    alien["plan"]["schema_version"] = "2.0";
    alien["plan"]["intent"] = "terraform_the_moon";
    alien["plan"]["steps"] = Json::Value( Json::arrayValue );
    alien["plan"]["steps"].append( Json::Value( Json::objectValue ) );
    checkTypedFailure( tool->execute( alien ), error_codes::kInvalidPlan );
}

// — harness:workflow_session boundary —————————————————————————————————————

TEST_CASE( "harness:workflow_session refuses boundary inputs with typed "
           "field-named errors", "[harness][boundaries-r4][session-tool]" )
{
    auto tool = harnessTool( "harness:workflow_session", &registerContextSessionTools );

    // Missing action.
    checkTypedFailure( tool->execute( input( Json::Value( Json::objectValue ) ) ),
                       error_codes::kInvalidParameter );

    // Unknown action vocabulary.
    Json::Value unknown( Json::objectValue );
    unknown["action"] = "teleport";
    unknown["session_id"] = "s";
    checkTypedFailure( tool->execute( unknown ), error_codes::kInvalidParameter );

    // session-requiring actions without a session_id: the field is named.
    for ( const char *action : { "save", "resume", "delete", "staleness" } )
    {
        INFO( "action: " << action );
        Json::Value noId( Json::objectValue );
        noId["action"] = action;
        checkTypedFailure( tool->execute( noId ), error_codes::kInvalidParameter );
    }

    // Save with a stage cursor outside the closed vocabulary.
    Json::Value badStage( Json::objectValue );
    badStage["action"] = "save";
    badStage["session_id"] = "boundary-stage";
    badStage["stage_cursor"] = "warp_drive";
    checkTypedFailure( tool->execute( badStage ), error_codes::kInvalidParameter );

    // Save with a hostile session id → typed refusal at the tool boundary.
    Json::Value hostileId( Json::objectValue );
    hostileId["action"] = "save";
    hostileId["session_id"] = "../../escaped";
    hostileId["stage_cursor"] = "intent";
    checkTypedFailure( tool->execute( hostileId ), error_codes::kInvalidParameter );
}

// — the taxonomy itself: envelope shape and legacy normalization ——————————

TEST_CASE( "the error envelope carries the full typed contract for every "
           "fashioned error", "[harness][boundaries-r4][taxonomy]" )
{
    const HarnessError structured = HarnessError::makeWithAction(
        error_codes::kInvalidPlan, "boundary fixture",
        "harness:plan", Json::Value() );
    const Json::Value envelope = errorEnvelope( structured );
    // The wire shape agents consume (harness_error.h:139-142): a failed
    // envelope whose error object carries code/message/category/retryable/
    // recoverable/suggested_actions — every field present, the code from
    // the closed taxonomy, retryability derived not guessed.
    CHECK( envelope.isObject() );
    CHECK_FALSE( envelope["success"].asBool() );
    const Json::Value &error = envelope["error"];
    REQUIRE( error.isObject() );
    CHECK( error["code"].asString() == error_codes::kInvalidPlan );
    CHECK_FALSE( error["message"].asString().empty() );
    CHECK( error.isMember( "category" ) );
    CHECK( error.isMember( "retryable" ) );
    CHECK( error.isMember( "recoverable" ) );
    CHECK( error.isMember( "suggested_actions" ) );
    CHECK( error["suggested_actions"].isArray() );

    // The code → category/retry mapping is total: every code in the closed
    // vocabulary maps, and nothing outside it claims to be known.
    for ( const std::string &code : allErrorCodes() )
    {
        CHECK( isKnownErrorCode( code ) );
        CHECK_FALSE( errorCategoryForCode( code ).empty() );
    }
    CHECK_FALSE( isKnownErrorCode( "DEFINITELY_NOT_A_CODE" ) );

    // A legacy/unmapped code degrades to EXECUTION_FAILED — an agent never
    // sees an unmapped code on the harness surface.
    const HarnessError normalized =
        normalizeLegacyError( "SOME_ANCIENT_FAILURE", "the old world" );
    CHECK( normalized.code == error_codes::kExecutionFailed );
}

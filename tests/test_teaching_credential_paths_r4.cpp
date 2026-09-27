/***************************************************************************
  tests/test_teaching_credential_paths_r4.cpp — WP-D (Track 12, teaching-lab
  R4): teacher-credential fail-closed CONSISTENCY across the three consumer
  seams of SICNU_LAB_TEACHER_TOKEN.

  All teacher-credential consumers funnel through
  sicnu::agent::harness::teacherCredentialValid(); the seams are still
  asserted independently (public surfaces only, no internal probes) so a
  future split of the gate cannot silently drift one path open:

    path 1  labAsk()            — teacher-surface intent (routed_tool bypass
                                  pins the intent deterministically)
    path 2  harness:autonomy_status — the privileged session autonomy layer
                                  (honored ONLY with the credential)
    path 3  labReference()      — grade_citation (grading reference surface)

  Token states asserted per seam: unset / empty / wrong (same length, one
  byte flipped — the case a length-leaking compare would miss) / correct.
  The refusal envelope must carry the EXISTING TEACHING_REFUSAL code (no new
  error-code axis; vocabulary of src/agent/harness/harness_error.h).
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cstdlib>
#include <string>

#include "agent/harness/autonomy_tools.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/lab_copilot.h"
#include "agent/spatial_tools/spatial_tool.h"

using namespace sicnu::agent::harness;
using namespace sicnu::agent::spatial_tools;

namespace
{

/// Save/restore the real host token around one test case so the binary can
/// run on machines that legitimately export the variable.
class TokenEnvGuard
{
  public:
    explicit TokenEnvGuard( const char *value )
    {
        if ( const char *old = std::getenv( kEnv ) )
            saved_ = old;
        if ( value )
            ::setenv( kEnv, value, 1 );
        else
            ::unsetenv( kEnv );
    }
    ~TokenEnvGuard()
    {
        if ( !saved_.empty() )
            ::setenv( kEnv, saved_.c_str(), 1 );
        else
            ::unsetenv( kEnv );
    }
    TokenEnvGuard( const TokenEnvGuard & ) = delete;
    TokenEnvGuard &operator=( const TokenEnvGuard & ) = delete;

  private:
    static constexpr const char *kEnv = "SICNU_LAB_TEACHER_TOKEN";
    std::string saved_;
};

constexpr const char *kHostToken = "r4-host-injected-token-9f2a";
/// Same length as kHostToken, exactly one byte flipped — the adversarial
/// case for any comparison that leaks its result through length or early
/// exit (the shape of the historical `return diff;` regression).
constexpr const char *kWrongToken = "r4-host-injected-token-9f2b";

Json::Value teacherAsk( const char *token )
{
    Json::Value input;
    input["role"] = "teacher";
    // routed_tool bypass: execute-shaped regardless of prose (the contract
    // treats any non-empty routed_tool as kIntentLabExecute), so the test
    // does not depend on the prose classifier's keyword table.
    input["routed_tool"] = "harness:lab_execute";
    if ( token )
        input["teacher_token"] = token;
    return labAsk( input );
}

Json::Value gradeCitation( const char *token )
{
    Json::Value input;
    input["role"] = "teacher";
    input["kind"] = "grade_citation";
    if ( token )
        input["teacher_token"] = token;
    return labReference( input );
}

/// The autonomy seam: harness:autonomy_status with (and without) a
/// privileged session block. Returns both projections for differential
/// comparison by the caller.
void autonomyProjections( const char *token, Json::Value &withBlock, Json::Value &withoutBlock )
{
    registerAutonomyTools();
    const auto tool = SpatialToolRegistry::instance().find( "harness:autonomy_status" );
    REQUIRE( tool.has_value() );

    Json::Value plain;
    plain["role"] = "teacher";
    if ( token )
        plain["teacher_token"] = token;
    withoutBlock = tool.value()->execute( plain ).output;

    Json::Value elevated = plain;
    Json::Value session;
    session["schema"] = "sicnu.autonomy-policy/1"; // parseAutonomyPolicy requires it
    session["level"] = "L3";
    elevated["autonomy"] = session;
    withBlock = tool.value()->execute( elevated ).output;
}

bool isTeachingRefusal( const Json::Value &envelope )
{
    return !envelope["success"].asBool() &&
           envelope["error"]["code"].asString() == error_codes::kTeachingRefusal &&
           envelope["refusal"]["refused"].asBool();
}

} // namespace

TEST_CASE( "credential paths r4: unset env fail-closes every teacher seam", "[teaching_r4][credential]" )
{
    TokenEnvGuard guard( nullptr );

    CHECK( isTeachingRefusal( teacherAsk( kHostToken ) ) );
    CHECK( isTeachingRefusal( gradeCitation( kHostToken ) ) );

    Json::Value withBlock, withoutBlock;
    autonomyProjections( kHostToken, withBlock, withoutBlock );
    // Without the credential the privileged session layer must be ignored:
    // presenting an L3 block changes nothing the caller can see.
    CHECK( withBlock == withoutBlock );
}

TEST_CASE( "credential paths r4: empty env value fail-closes every teacher seam",
           "[teaching_r4][credential]" )
{
    TokenEnvGuard guard( "" );

    CHECK( isTeachingRefusal( teacherAsk( kHostToken ) ) );
    CHECK( isTeachingRefusal( gradeCitation( kHostToken ) ) );

    Json::Value withBlock, withoutBlock;
    autonomyProjections( kHostToken, withBlock, withoutBlock );
    CHECK( withBlock == withoutBlock );
}

TEST_CASE( "credential paths r4: same-length wrong token fail-closes every teacher seam",
           "[teaching_r4][credential]" )
{
    TokenEnvGuard guard( kHostToken );

    CHECK( isTeachingRefusal( teacherAsk( kWrongToken ) ) );
    CHECK( isTeachingRefusal( gradeCitation( kWrongToken ) ) );

    Json::Value withBlock, withoutBlock;
    autonomyProjections( kWrongToken, withBlock, withoutBlock );
    CHECK( withBlock == withoutBlock );
}

TEST_CASE( "credential paths r4: non-string and empty provided tokens fail closed",
           "[teaching_r4][credential]" )
{
    TokenEnvGuard guard( kHostToken );

    Json::Value nonString;
    nonString["role"] = "teacher";
    nonString["routed_tool"] = "harness:lab_execute";
    nonString["teacher_token"] = 12345; // a model-stuffed non-string claim
    CHECK( isTeachingRefusal( labAsk( nonString ) ) );

    Json::Value emptyToken;
    emptyToken["role"] = "teacher";
    emptyToken["kind"] = "grade_citation";
    emptyToken["teacher_token"] = "";
    CHECK( isTeachingRefusal( labReference( emptyToken ) ) );
}

TEST_CASE( "credential paths r4: correct token opens every teacher seam consistently",
           "[teaching_r4][credential]" )
{
    TokenEnvGuard guard( kHostToken );

    // Path 1: the teacher surface is reachable — role stays teacher (no
    // refusal envelope), the execute intent answers through the teacher
    // surface pointer, not a refusal.
    Json::Value ask = teacherAsk( kHostToken );
    REQUIRE( ask["success"].asBool() );
    CHECK( ask["result"]["role"].asString() == "teacher" );
    CHECK( ask["refusal"].isNull() );

    // Path 3: grading reference opens and degrades HONESTLY when the grade
    // authority is not wired — typed unavailable, never a fabricated score.
    Json::Value citation = gradeCitation( kHostToken );
    REQUIRE( citation["success"].asBool() );
    CHECK( citation["result"]["grade"]["status"].asString() == "unavailable" );

    // Path 2: with the credential the privileged session layer IS honored —
    // an L3 block must change the projection (differential against the same
    // input without the block).
    Json::Value withBlock, withoutBlock;
    autonomyProjections( kHostToken, withBlock, withoutBlock );
    CHECK( withBlock != withoutBlock );
    CHECK( withBlock["level"].asString() == "L3" );
    CHECK( withoutBlock["level"].asString() != "L3" );
}

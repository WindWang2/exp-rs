// test_preflight_render.cpp — RS14-02 slice B: teaching/agent dual projection.
//
// One report, two projections (single truth source):
//   teaching — sicnu.preflight.teaching/1, quotes findings and evidence
//              verbatim, differentiates block vs require_ack next steps,
//              emits an explicit all-clear skeleton on clean runs.
//   agent    — sicnu.preflight.agent/1, verdict + required_actions ONLY;
//              the agent surface can never repair, execute or fabricate.

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "preflight/engine.h"
#include "preflight/finding.h"
#include "preflight/provider.h"
#include "preflight/render.h"
#include "preflight/report.h"
#include "preflight/rule.h"
#include "preflight/sha256.h"

#include <string>
#include <vector>

using namespace sicnu::preflight;

namespace {

PreflightFinding ackFinding()
{
    PreflightFinding f;
    f.code = "SPF_RADIOMETRIC_STATE_MISMATCH";
    f.severity = PreflightSeverity::RequireAck;
    f.ruleId = "preflight.radiometric_state_policy";
    f.ruleRevision = 1;
    f.domain = "radiometric";
    f.subject = "primary";
    f.basis = "declared";
    f.humanExplanation = "NDVI 需要反射率域输入，而 primary 声明为 raw DN。";
    f.machineExplanation["expectation"] = "radiometric_state in [surface_reflectance]";
    f.machineExplanation["actual"] = "dn";
    return f;
}

PreflightFinding blockFinding()
{
    PreflightFinding f;
    f.code = "SPF_BAND_ROLE_MISSING";
    f.severity = PreflightSeverity::Block;
    f.ruleId = "preflight.band_role";
    f.ruleRevision = 1;
    f.domain = "spectral";
    f.subject = "primary";
    f.basis = "observed";
    f.humanExplanation = "缺少 NIR 波段。";
    return f;
}

PreflightReport reportWith( std::vector<PreflightFinding> findings, std::string verdict )
{
    PreflightReport r = PreflightReport::makeEmpty( "op-1", "teaching" );
    r.verdict = std::move( verdict );
    r.requestDigest = shortDigest( "req" );
    r.rulesRevision = shortDigest( "rules" );
    r.findings = std::move( findings );
    PreflightEvaluatedRule e;
    e.ruleId = "preflight.band_role";
    e.revision = 1;
    e.outcome = r.findings.empty() ? "pass" : "finding";
    r.evaluated.push_back( e );
    return r;
}

/// No projection may expose a repair/execution surface: scan every key and
/// string value recursively for repair-vocabulary keys.
bool containsKey( const Json::Value &node, const std::string &key )
{
    if ( node.isObject() )
    {
        for ( const auto &name : node.getMemberNames() )
        {
            if ( name == key )
                return true;
            if ( containsKey( node[name], key ) )
                return true;
        }
    }
    else if ( node.isArray() )
    {
        for ( const auto &item : node )
            if ( containsKey( item, key ) )
                return true;
    }
    return false;
}

} // namespace

TEST_CASE( "teaching projection: schema, verbatim quotes, all-clear skeleton",
           "[preflight][render]" )
{
    const PreflightReport clean = reportWith( {}, "ok" );
    const Json::Value teaching = renderTeaching( clean );
    REQUIRE( teaching["schema"].asString() == "sicnu.preflight.teaching/1" );
    REQUIRE( teaching["schema_version"].asString() == "1" );
    REQUIRE( teaching["kind"].asString() == "preflight_teaching" );
    REQUIRE( teaching["verdict"].asString() == "ok" );
    REQUIRE( teaching["request_digest"].asString() == clean.requestDigest );
    // Explicit all-clear skeleton — silence is not allowed.
    REQUIRE( teaching["all_clear"].isObject() );
    REQUIRE_FALSE( teaching["all_clear"]["summary"].asString().empty() );
    REQUIRE( teaching["all_clear"]["checks_run"].asInt() == 1 );
    REQUIRE( ( teaching["items"].isNull() || teaching["items"].empty() ) );

    // Deterministic bytes.
    REQUIRE( canonicalProjectionJson( teaching ) == canonicalProjectionJson( renderTeaching( clean ) ) );
}

TEST_CASE( "teaching projection quotes finding and evidence verbatim",
           "[preflight][render]" )
{
    const Json::Value teaching = renderTeaching( reportWith( { ackFinding() }, "requires_ack" ) );
    const Json::Value items = teaching["items"];
    REQUIRE( items.size() == 1 );
    const Json::Value item = items[0];
    REQUIRE( item["code"].asString() == "SPF_RADIOMETRIC_STATE_MISMATCH" );
    // The human explanation and the machine evidence are quoted verbatim —
    // the teaching layer adds framing, never paraphrases facts.
    REQUIRE( item["situation"].asString() == ackFinding().humanExplanation );
    REQUIRE( item["evidence"] == ackFinding().evidence );
}

TEST_CASE( "teaching next_step semantics differ for block vs require_ack",
           "[preflight][render]" )
{
    // Unacknowledged require_ack names the acknowledgement path.
    const Json::Value ackView =
        renderTeaching( reportWith( { ackFinding() }, "requires_ack" ) )["items"][0];
    REQUIRE( ackView["next_step"].asString().find( "acknowledge" ) != std::string::npos );
    REQUIRE( ackView["next_step"].asString().find( "SPF_RADIOMETRIC_STATE_MISMATCH" ) !=
             std::string::npos );

    // A block must never offer acknowledgement as the next step.
    const Json::Value blockView =
        renderTeaching( reportWith( { blockFinding() }, "blocked" ) )["items"][0];
    REQUIRE( blockView["next_step"].asString().find( "acknowledge" ) == std::string::npos );

    // Acknowledged require_ack records who accepted it.
    PreflightFinding acked = ackFinding();
    acked.acknowledged = true;
    const Json::Value accepted =
        renderTeaching( reportWith( { acked }, "ok" ) )["items"][0];
    REQUIRE( accepted["next_step"].asString().find( "acknowledged" ) != std::string::npos );
}

TEST_CASE( "agent projection: schema, judgments mirror findings, required_actions exact",
           "[preflight][render]" )
{
    PreflightFinding risk = ackFinding();
    PreflightReport report = reportWith( { risk, blockFinding() }, "blocked" );

    const Json::Value agent = renderAgent( report );
    REQUIRE( agent["schema"].asString() == "sicnu.preflight.agent/1" );
    REQUIRE( agent["schema_version"].asString() == "1" );
    REQUIRE( agent["verdict"].asString() == "blocked" );
    REQUIRE( agent["request_digest"].asString() == report.requestDigest );
    REQUIRE( agent["can_proceed"].asBool() == false );

    // Judgments: code/severity/subject only, in report order.
    const Json::Value judgments = agent["judgments"];
    REQUIRE( judgments.size() == 2 );
    REQUIRE( judgments[0]["code"].asString() == "SPF_RADIOMETRIC_STATE_MISMATCH" );
    REQUIRE( judgments[0]["severity"].asString() == "require_ack" );
    REQUIRE( judgments[1]["code"].asString() == "SPF_BAND_ROLE_MISSING" );

    // Required actions: one revise_inputs for the block. The require_ack
    // finding is unacknowledged, so an acknowledge action is also required.
    const Json::Value actions = agent["required_actions"];
    REQUIRE( actions.size() == 2 );
    bool sawAck = false;
    bool sawRevise = false;
    for ( const auto &a : actions )
    {
        if ( a["action"].asString() == "acknowledge" )
        {
            sawAck = true;
            REQUIRE( a["code"].asString() == "SPF_RADIOMETRIC_STATE_MISMATCH" );
        }
        if ( a["action"].asString() == "revise_inputs" )
        {
            sawRevise = true;
            REQUIRE( a["code"].asString() == "SPF_BAND_ROLE_MISSING" );
        }
    }
    REQUIRE( sawAck );
    REQUIRE( sawRevise );
}

TEST_CASE( "agent projection exposes no repair or execution surface",
           "[preflight][render]" )
{
    PreflightReport report = reportWith( { ackFinding(), blockFinding() }, "blocked" );
    const Json::Value agent = renderAgent( report );
    const Json::Value teaching = renderTeaching( report );
    for ( const std::string &forbidden : { "repair", "execute", "apply", "fix" } )
    {
        REQUIRE_FALSE( containsKey( agent, forbidden ) );
        REQUIRE_FALSE( containsKey( teaching, forbidden ) );
    }
    // The agent projection carries no human explanation or evidence dump:
    // it is judgments + actions only.
    REQUIRE( agent["judgments"][0]["human_explanation"].isNull() );
    REQUIRE( agent["judgments"][0]["evidence"].isNull() );
}

TEST_CASE( "both projections agree on the same report (single truth source)",
           "[preflight][render]" )
{
    const PreflightReport report = reportWith( { ackFinding() }, "requires_ack" );
    const Json::Value teaching = renderTeaching( report );
    const Json::Value agent = renderAgent( report );

    REQUIRE( teaching["verdict"].asString() == agent["verdict"].asString() );
    REQUIRE( teaching["request_digest"].asString() == agent["request_digest"].asString() );
    REQUIRE( teaching["items"].size() == agent["judgments"].size() );
    REQUIRE( teaching["items"][0]["code"].asString() == agent["judgments"][0]["code"].asString() );
}

TEST_CASE( "projections are byte-deterministic", "[preflight][render]" )
{
    const PreflightReport report = reportWith( { ackFinding(), blockFinding() }, "blocked" );
    REQUIRE( canonicalProjectionJson( renderTeaching( report ) ) ==
             canonicalProjectionJson( renderTeaching( report ) ) );
    REQUIRE( canonicalProjectionJson( renderAgent( report ) ) ==
             canonicalProjectionJson( renderAgent( report ) ) );
    const std::string bytes = canonicalProjectionJson( renderAgent( report ) );
    REQUIRE( bytes.find( "timestamp" ) == std::string::npos );
    REQUIRE( bytes.find( "generated_at" ) == std::string::npos );
}

TEST_CASE( "agent projection of a clean report allows proceeding with no actions",
           "[preflight][render]" )
{
    const PreflightReport clean = reportWith( {}, "ok" );
    const Json::Value agent = renderAgent( clean );
    REQUIRE( agent["can_proceed"].asBool() == true );
    REQUIRE( agent["required_actions"].empty() );
}

#include "preflight/render.h"

namespace sicnu::preflight {
namespace {

std::string teachingNextStep( const PreflightFinding &finding )
{
    switch ( finding.severity )
    {
        case PreflightSeverity::Block:
            return "Blocked: revise the inputs or choose another operator; no acceptance can "
                   "clear this finding.";
        case PreflightSeverity::RequireAck:
            if ( finding.acknowledged )
                return "Risk acknowledged by the operator; the finding stays recorded in the "
                       "report.";
            return "Requires an explicit acknowledgement of code " + finding.code +
                   " before the run can proceed.";
        case PreflightSeverity::Warn:
            return "Advisory only; it does not gate the run.";
        case PreflightSeverity::Info:
            return "Boundary note; no action required.";
    }
    return "";
}

Json::Value envelope( const PreflightReport &report, const char *schema, const char *kind )
{
    Json::Value j( Json::objectValue );
    j["schema"] = schema;
    j["schema_version"] = "1";
    j["kind"] = kind;
    j["request_digest"] = report.requestDigest;
    j["rules_revision"] = report.rulesRevision;
    j["verdict"] = report.verdict;
    return j;
}

} // namespace

Json::Value renderTeaching( const PreflightReport &report )
{
    Json::Value teaching = envelope( report, "sicnu.preflight.teaching/1", "preflight_teaching" );

    Json::Value items( Json::arrayValue );
    for ( const auto &finding : report.findings )
    {
        Json::Value item( Json::objectValue );
        item["code"] = finding.code;
        item["severity"] = severityToString( finding.severity );
        item["subject"] = finding.subject;
        // Verbatim quotes: the teaching layer adds framing around the
        // finding's own words, it never paraphrases facts.
        item["situation"] = finding.humanExplanation;
        item["evidence"] = finding.evidence;
        item["machine_explanation"] = finding.machineExplanation;
        item["next_step"] = teachingNextStep( finding );
        items.append( item );
    }
    teaching["items"] = items;

    if ( report.verdict == "ok" && report.findings.empty() )
    {
        Json::Value allClear( Json::objectValue );
        allClear["summary"] = "No scientific blockers found; every registered rule evaluated.";
        allClear["checks_run"] = static_cast<int>( report.evaluated.size() );
        allClear["rules_revision"] = report.rulesRevision;
        teaching["all_clear"] = allClear;
    }
    return teaching;
}

Json::Value renderAgent( const PreflightReport &report )
{
    Json::Value agent = envelope( report, "sicnu.preflight.agent/1", "preflight_agent_projection" );

    Json::Value judgments( Json::arrayValue );
    Json::Value actions( Json::arrayValue );
    for ( const auto &finding : report.findings )
    {
        Json::Value judgment( Json::objectValue );
        judgment["code"] = finding.code;
        judgment["severity"] = severityToString( finding.severity );
        judgment["subject"] = finding.subject;
        judgments.append( judgment );

        // Required actions, derived exactly from the findings: a block means
        // the request must be revised (never "repair" — the agent surface
        // executes nothing), an unacknowledged require_ack names the code to
        // accept. Acknowledged, warn and info findings require no action.
        Json::Value action( Json::objectValue );
        if ( finding.severity == PreflightSeverity::Block )
        {
            action["action"] = "revise_inputs";
            action["code"] = finding.code;
            action["subject"] = finding.subject;
            actions.append( action );
        }
        else if ( finding.severity == PreflightSeverity::RequireAck && !finding.acknowledged )
        {
            action["action"] = "acknowledge";
            action["code"] = finding.code;
            action["subject"] = finding.subject;
            actions.append( action );
        }
    }
    agent["judgments"] = judgments;
    agent["required_actions"] = actions;
    agent["can_proceed"] = report.verdict == "ok";
    return agent;
}

std::string canonicalProjectionJson( const Json::Value &projection )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    builder["precision"] = 12;
    builder["precisionType"] = "significant";
    builder["emitUTF8"] = true;
    return Json::writeString( builder, projection );
}

} // namespace sicnu::preflight

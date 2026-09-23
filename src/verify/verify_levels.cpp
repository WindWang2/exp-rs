// src/verify/verify_levels.cpp — two-level verification (node postcondition
// and whole-task outcome) for the Unified Scientific Verifier.
#include "verify_levels.h"

#include "verify_engine.h"
#include "verify_error_codes.h"
#include "verify_sha256.h"

#include <set>
#include <string>
#include <vector>

namespace sicnu::verify
{

namespace
{

VerificationCheckResult syntheticSpecValidFail( const std::string &message )
{
    VerificationCheckResult result;
    result.checkId = "spec.valid";
    result.kind = "spec.valid";
    result.status = VerificationStatus::Fail;
    result.code = kCodeInvalidSpec;
    result.message = message;
    return result;
}

Json::Value checkResultToJson( const VerificationCheckResult &check )
{
    // Same serialization contract as VerificationReport::toCanonicalJson —
    // the outcome body and the report body share one check-result shape.
    Json::Value json( Json::objectValue );
    json["checkId"] = check.checkId;
    json["kind"] = check.kind;
    json["status"] = statusToWire( check.status );
    if ( !check.code.empty() )
        json["code"] = check.code;
    if ( !check.message.empty() )
        json["message"] = check.message;
    if ( check.evidence )
        json["evidence"] = check.evidence->toCanonicalJson();
    return json;
}

} // namespace

VerificationReport verifyPlanNodePostcondition( const VerificationSpec &spec,
                                                const VerificationContext &context )
{
    if ( spec.scope != "node" )
    {
        const std::string digest = specDigest( spec );
        return buildReport( spec.specId, spec.scope, digest,
                            { syntheticSpecValidFail( "spec scope '" + spec.scope +
                                                      "' cannot judge a node postcondition" ) } );
    }
    return evaluate( spec, context );
}

Json::Value TaskOutcome::toCanonicalJson() const
{
    Json::Value doc( Json::objectValue );
    doc["schema"] = kTaskOutcomeSchema;
    doc["taskSpecId"] = taskSpecId;
    doc["taskSpecDigest"] = taskSpecDigest;
    doc["overall"] = statusToWire( overall );
    Json::Value nodesJson( Json::arrayValue );
    for ( const NodeOutcome &node : nodes )
    {
        Json::Value nodeJson( Json::objectValue );
        nodeJson["specId"] = node.specId;
        nodeJson["overall"] = statusToWire( node.overall );
        nodeJson["reportDigest"] = node.reportDigest;
        nodesJson.append( nodeJson );
    }
    doc["nodes"] = nodesJson;
    Json::Value checksJson( Json::arrayValue );
    for ( const VerificationCheckResult &check : taskChecks )
        checksJson.append( checkResultToJson( check ) );
    doc["taskChecks"] = checksJson;
    return doc;
}

std::string TaskOutcome::digest() const
{
    const std::string text = canonicalJsonText( toCanonicalJson() );
    return text.empty() ? std::string{} : sha256Hex( text );
}

TaskOutcome verifyWholeTask( const VerificationSpec &taskSpec, const VerificationContext &context,
                             const std::vector<VerificationReport> &nodeReports )
{
    TaskOutcome outcome;
    outcome.taskSpecId = taskSpec.specId;
    outcome.taskSpecDigest = specDigest( taskSpec );

    if ( taskSpec.scope != "task" )
    {
        outcome.taskChecks.push_back(
            syntheticSpecValidFail( "spec scope '" + taskSpec.scope + "' cannot judge a whole-task outcome" ) );
        outcome.overall = aggregateStatus( { VerificationStatus::Fail } );
        return outcome;
    }

    const VerificationReport report = evaluate( taskSpec, context );
    outcome.taskSpecDigest = report.specDigest;
    outcome.taskChecks = report.checks;

    for ( const VerificationReport &node : nodeReports )
    {
        NodeOutcome nodeOutcome;
        nodeOutcome.specId = node.specId;
        // Defensive: derive the folded status from the CHECKS (the atomic
        // facts), not from the report's own overall field — an in-memory
        // report with a forged overall cannot sway the task rollup.
        std::vector<VerificationStatus> nodeStatuses;
        nodeStatuses.reserve( node.checks.size() );
        for ( const VerificationCheckResult &check : node.checks )
            nodeStatuses.push_back( check.status );
        nodeOutcome.overall = aggregateStatus( nodeStatuses );
        nodeOutcome.reportDigest = node.digest();
        outcome.nodes.push_back( std::move( nodeOutcome ) );
    }

    std::vector<VerificationStatus> statuses;
    statuses.reserve( outcome.nodes.size() + outcome.taskChecks.size() );
    for ( const NodeOutcome &node : outcome.nodes )
        statuses.push_back( node.overall );
    for ( const VerificationCheckResult &check : outcome.taskChecks )
        statuses.push_back( check.status );
    outcome.overall = aggregateStatus( statuses );
    return outcome;
}

// ---------------------------------------------------------------------------
// Strict outcome reader
// ---------------------------------------------------------------------------

namespace
{

const std::set<std::string> &outcomeTopFields()
{
    static const std::set<std::string> fields = { "schema", "taskSpecId", "taskSpecDigest", "overall",
                                                  "nodes", "taskChecks" };
    return fields;
}

const std::set<std::string> &outcomeNodeFields()
{
    static const std::set<std::string> fields = { "specId", "overall", "reportDigest" };
    return fields;
}

const std::set<std::string> &outcomeCheckFields()
{
    static const std::set<std::string> fields = { "checkId", "kind", "status", "code", "message",
                                                  "evidence" };
    return fields;
}

bool hasUnknownField( const Json::Value &object, const std::set<std::string> &allowed, std::string &unknown )
{
    for ( const std::string &member : object.getMemberNames() )
    {
        if ( !allowed.count( member ) )
        {
            unknown = member;
            return true;
        }
    }
    return false;
}

bool parseStatusWire( const std::string &wire, VerificationStatus &out )
{
    if ( wire == "pass" )
        out = VerificationStatus::Pass;
    else if ( wire == "fail" )
        out = VerificationStatus::Fail;
    else if ( wire == "indeterminate" )
        out = VerificationStatus::Indeterminate;
    else
        return false;
    return true;
}

bool isHex64( const std::string &text )
{
    if ( text.size() != 64 )
        return false;
    for ( const char c : text )
    {
        const bool hex = ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );
        if ( !hex )
            return false;
    }
    return true;
}

} // namespace

bool TaskOutcome::fromCanonicalJson( const Json::Value &json, TaskOutcome &out, std::string &error,
                                     const std::string &expectedDigest )
{
    const auto fail = [ &error ]( const std::string &reason ) {
        error = reason;
        return false;
    };
    if ( !json.isObject() )
        return fail( "task outcome document must be a JSON object" );
    // Defense in depth, mirroring the report reader: legit producers cannot
    // seal a non-finite body, so a hand-forged one must not parse either.
    if ( jsonCarriesNonFiniteNumber( json ) )
        return fail( "task outcome body carries a non-finite number and cannot be sealed" );
    std::string unknown;
    if ( hasUnknownField( json, outcomeTopFields(), unknown ) )
        return fail( "unknown task outcome field: '" + unknown + "'" );
    if ( !json["schema"].isString() || json["schema"].asString() != kTaskOutcomeSchema )
        return fail( std::string( "schema marker must be " ) + kTaskOutcomeSchema );
    if ( !json["taskSpecId"].isString() || json["taskSpecId"].asString().empty() )
        return fail( "'taskSpecId' must be a non-empty string" );
    if ( !json["taskSpecDigest"].isString() || !isHex64( json["taskSpecDigest"].asString() ) )
        return fail( "'taskSpecDigest' must be a 64-char hex sha256" );
    VerificationStatus overall;
    if ( !json["overall"].isString() || !parseStatusWire( json["overall"].asString(), overall ) )
        return fail( "'overall' must be a status wire string" );
    if ( !json["nodes"].isArray() || !json["taskChecks"].isArray() )
        return fail( "'nodes' and 'taskChecks' must be arrays" );

    TaskOutcome parsed;
    parsed.taskSpecId = json["taskSpecId"].asString();
    parsed.taskSpecDigest = json["taskSpecDigest"].asString();
    parsed.overall = overall;

    for ( const Json::Value &nodeJson : json["nodes"] )
    {
        if ( !nodeJson.isObject() )
            return fail( "each node outcome must be an object" );
        if ( hasUnknownField( nodeJson, outcomeNodeFields(), unknown ) )
            return fail( "unknown node outcome field: '" + unknown + "'" );
        if ( !nodeJson["specId"].isString() || nodeJson["specId"].asString().empty() )
            return fail( "node 'specId' must be a non-empty string" );
        NodeOutcome node;
        node.specId = nodeJson["specId"].asString();
        if ( !nodeJson["overall"].isString() || !parseStatusWire( nodeJson["overall"].asString(), node.overall ) )
            return fail( "node '" + node.specId + "': bad overall wire string" );
        // 64-hex, or the empty refusal sentinel for an unsealable node body.
        if ( !nodeJson["reportDigest"].isString() ||
             !( nodeJson["reportDigest"].asString().empty() || isHex64( nodeJson["reportDigest"].asString() ) ) )
            return fail( "node '" + node.specId + "': 'reportDigest' must be sha256 hex or empty" );
        node.reportDigest = nodeJson["reportDigest"].asString();
        parsed.nodes.push_back( std::move( node ) );
    }

    // Check results follow the SAME rules as report bodies: closed wire
    // vocabulary, code class tied to status, evidence strict. Mirrors
    // VerificationReport::fromCanonicalJson — the two readers must evolve
    // together (locked by the interlock test in test_verifier_adversarial).
    std::vector<VerificationStatus> statuses;
    for ( const Json::Value &checkJson : json["taskChecks"] )
    {
        if ( !checkJson.isObject() )
            return fail( "each task check must be an object" );
        if ( hasUnknownField( checkJson, outcomeCheckFields(), unknown ) )
            return fail( "unknown task check field: '" + unknown + "'" );
        VerificationCheckResult check;
        if ( !checkJson["checkId"].isString() || checkJson["checkId"].asString().empty() )
            return fail( "'checkId' must be a non-empty string" );
        if ( !checkJson["kind"].isString() || checkJson["kind"].asString().empty() )
            return fail( "'kind' must be a non-empty string" );
        check.checkId = checkJson["checkId"].asString();
        check.kind = checkJson["kind"].asString();
        if ( !checkJson["status"].isString() || !parseStatusWire( checkJson["status"].asString(), check.status ) )
            return fail( "check '" + check.checkId + "': bad status wire string" );
        if ( checkJson.isMember( "code" ) )
        {
            if ( !checkJson["code"].isString() )
                return fail( "'code' must be a string" );
            check.code = checkJson["code"].asString();
        }
        if ( checkJson.isMember( "message" ) )
        {
            if ( !checkJson["message"].isString() )
                return fail( "'message' must be a string" );
            check.message = checkJson["message"].asString();
        }
        if ( check.status == VerificationStatus::Pass && !check.code.empty() )
            return fail( "check '" + check.checkId + "': a pass must not carry a code" );
        if ( check.status != VerificationStatus::Pass )
        {
            if ( check.code.empty() )
                return fail( "check '" + check.checkId + "': non-pass result needs a typed code" );
            if ( !isVerifierCode( check.code ) )
                return fail( "check '" + check.checkId + "': code '" + check.code +
                             "' is outside the verifier vocabulary" );
            if ( ( check.status == VerificationStatus::Indeterminate ) != isIndeterminateCode( check.code ) )
                return fail( "check '" + check.checkId + "': code class contradicts the status" );
        }
        if ( checkJson.isMember( "evidence" ) )
        {
            VerificationEvidence evidence;
            std::string evidenceError;
            if ( !evidence.fromCanonicalJson( checkJson["evidence"], evidenceError ) )
                return fail( "check '" + check.checkId + "': " + evidenceError );
            check.evidence = std::move( evidence );
        }
        statuses.push_back( check.status );
        parsed.taskChecks.push_back( std::move( check ) );
    }

    for ( const NodeOutcome &node : parsed.nodes )
        statuses.push_back( node.overall );
    if ( aggregateStatus( statuses ) != parsed.overall )
        return fail( "'overall' disagrees with the nodes and task checks" );

    if ( !expectedDigest.empty() && parsed.digest() != expectedDigest )
        return fail( "task outcome digest mismatch: body does not hash to the expected digest" );

    out = std::move( parsed );
    error.clear();
    return true;
}

} // namespace sicnu::verify

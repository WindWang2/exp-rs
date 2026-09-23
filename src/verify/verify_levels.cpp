// src/verify/verify_levels.cpp — two-level verification (node postcondition
// and whole-task outcome) for the Unified Scientific Verifier.
#include "verify_levels.h"

#include "verify_engine.h"
#include "verify_error_codes.h"
#include "verify_sha256.h"

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
        nodeOutcome.overall = node.overall;
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

} // namespace sicnu::verify

// src/agent_loop/session_seams.cpp
#include "session_seams.h"

namespace sicnu::agent_loop {

Json::Value AssetFact::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "slot" ] = slot;
    doc[ "ref" ] = ref;
    doc[ "kind" ] = kind;
    doc[ "resolved" ] = resolved;
    doc[ "facts" ] = facts;
    if ( !resolutionError.empty() )
        doc[ "resolution_error" ] = resolutionError;
    return doc;
}

Json::Value DataStateSnapshot::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "schema_version" ] = schemaVersion;
    Json::Value assets( Json::arrayValue );
    for ( const AssetFact &asset : this->assets )
        assets.append( asset.toJson() );
    doc[ "assets" ] = assets;
    doc[ "summary" ] = summary;
    return doc;
}

std::optional< DataStateSnapshot > DataStateSnapshot::fromJson( const Json::Value &doc,
                                                                std::string *error )
{
    auto fail = [ &error ]( const std::string &message ) {
        if ( error )
            *error = message;
        return std::optional< DataStateSnapshot >{};
    };

    if ( !doc.isObject() )
        return fail( "data state snapshot is not an object" );
    std::string version;
    if ( !doc.isMember( "schema_version" ) || !doc[ "schema_version" ].isString() )
        return fail( "data state snapshot: missing schema_version" );
    version = doc[ "schema_version" ].asString();
    if ( version != "1.0" )
        return fail( "data state snapshot: unsupported schema_version '" + version + "'" );

    if ( !doc.isMember( "assets" ) || !doc[ "assets" ].isArray() )
        return fail( "data state snapshot: missing assets array" );

    DataStateSnapshot snapshot;
    snapshot.schemaVersion = version;
    for ( const Json::Value &assetDoc : doc[ "assets" ] )
    {
        if ( !assetDoc.isObject() )
            return fail( "data state snapshot: asset is not an object" );
        AssetFact asset;
        asset.slot = assetDoc.get( "slot", "" ).asString();
        asset.ref = assetDoc.get( "ref", "" ).asString();
        asset.kind = assetDoc.get( "kind", "" ).asString();
        asset.resolved = assetDoc.get( "resolved", false ).asBool();
        asset.facts = assetDoc.get( "facts", Json::Value( Json::objectValue ) );
        asset.resolutionError = assetDoc.get( "resolution_error", "" ).asString();
        snapshot.assets.push_back( asset );
    }
    snapshot.summary = doc.get( "summary", Json::Value( Json::objectValue ) );
    return snapshot;
}

long PlanDraft::totalRamMb() const
{
    long total = 0;
    for ( const PlanEstimate &estimate : estimates )
        total += estimate.ramMb;
    return total;
}

Json::Value PlanDraft::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "plan_id" ] = planId;
    doc[ "intent" ] = intent;
    doc[ "attempt" ] = attempt;
    doc[ "steps" ] = steps;
    doc[ "outputs" ] = outputs;
    doc[ "fingerprint" ] = fingerprint;
    doc[ "identity" ] = identity;
    doc[ "missing_facts" ] = missingFacts;
    Json::Value estimatesDoc( Json::arrayValue );
    for ( const PlanEstimate &estimate : estimates )
    {
        Json::Value entry( Json::objectValue );
        entry[ "step_id" ] = estimate.stepId;
        entry[ "ram_mb" ] = static_cast< Json::Int64 >( estimate.ramMb );
        estimatesDoc.append( entry );
    }
    doc[ "estimates" ] = estimatesDoc;
    Json::Value dropped( Json::arrayValue );
    for ( const DecisionAlternative &alt : droppedAlternatives )
    {
        Json::Value entry( Json::objectValue );
        entry[ "id" ] = alt.id;
        entry[ "description" ] = alt.description;
        entry[ "why_not" ] = alt.whyNot;
        dropped.append( entry );
    }
    doc[ "dropped_alternatives" ] = dropped;
    doc[ "valid" ] = valid;
    if ( !error.empty() )
        doc[ "error" ] = error;
    return doc;
}

Json::Value PreflightReport::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "verdict" ] = verdict;
    Json::Value issuesDoc( Json::arrayValue );
    for ( const PreflightIssue &issue : issues )
    {
        Json::Value entry( Json::objectValue );
        entry[ "code" ] = issue.code;
        entry[ "severity" ] = issue.severity;
        entry[ "message" ] = issue.message;
        issuesDoc.append( entry );
    }
    doc[ "issues" ] = issuesDoc;
    Json::Value proposalsDoc( Json::arrayValue );
    for ( const RepairProposal &proposal : proposals )
    {
        Json::Value entry( Json::objectValue );
        entry[ "rule_id" ] = proposal.ruleId;
        entry[ "risk_class" ] = proposal.riskClass;
        entry[ "operator_id" ] = proposal.operatorId;
        entry[ "arguments" ] = proposal.arguments;
        entry[ "rationale" ] = proposal.rationale;
        proposalsDoc.append( entry );
    }
    doc[ "proposals" ] = proposalsDoc;
    doc[ "checks" ] = checks;
    return doc;
}

Json::Value ArtifactVerificationReport::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "path" ] = path;
    doc[ "verdict" ] = verdict;
    Json::Value warningsDoc( Json::arrayValue );
    for ( const std::string &warning : warnings )
        warningsDoc.append( warning );
    doc[ "warnings" ] = warningsDoc;
    return doc;
}

std::string VerificationReport::aggregate( const std::vector< ArtifactVerificationReport > &artifacts )
{
    bool warning = false;
    for ( const ArtifactVerificationReport &artifact : artifacts )
    {
        if ( artifact.verdict == "FAIL" )
            return "FAIL";
        if ( artifact.verdict == "PASS_WITH_WARNINGS" )
            warning = true;
    }
    return warning ? "PASS_WITH_WARNINGS" : "PASS";
}

Json::Value VerificationReport::toJson() const
{
    Json::Value doc( Json::objectValue );
    Json::Value artifactsDoc( Json::arrayValue );
    for ( const ArtifactVerificationReport &artifact : artifacts )
        artifactsDoc.append( artifact.toJson() );
    doc[ "artifacts" ] = artifactsDoc;
    doc[ "verdict" ] = verdictValue;
    return doc;
}

Json::Value Diagnosis::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "root_cause_code" ] = rootCauseCode;
    doc[ "summary" ] = summary;
    doc[ "evidence" ] = evidence;
    Json::Value proposalsDoc( Json::arrayValue );
    for ( const RepairProposal &proposal : proposals )
    {
        Json::Value entry( Json::objectValue );
        entry[ "rule_id" ] = proposal.ruleId;
        entry[ "risk_class" ] = proposal.riskClass;
        entry[ "operator_id" ] = proposal.operatorId;
        entry[ "arguments" ] = proposal.arguments;
        entry[ "rationale" ] = proposal.rationale;
        proposalsDoc.append( entry );
    }
    doc[ "proposals" ] = proposalsDoc;
    return doc;
}

} // namespace sicnu::agent_loop

// src/repair_planner/repair_schema.cpp
#include "repair_schema.h"

#include "repair_sha256.h"

#include <set>

namespace sicnu::repair {

namespace {

const std::set<std::string> &riskClasses()
{
    static const std::set<std::string> kSet = { repair_risk::kShapePreserving,
                                                repair_risk::kRadiometric,
                                                repair_risk::kScienceChanging };
    return kSet;
}

const std::set<std::string> &actionKinds()
{
    static const std::set<std::string> kSet = { action_kind::kCapabilityRef,
                                                action_kind::kDeclarativeTransform,
                                                action_kind::kDecision };
    return kSet;
}

const std::set<std::string> &planStatuses()
{
    static const std::set<std::string> kSet = { plan_status::kPlanned,
                                                plan_status::kNoSafeRepair,
                                                plan_status::kInvalidInput };
    return kSet;
}

const std::set<std::string> &riskSeverities()
{
    static const std::set<std::string> kSet = { "low", "medium", "high" };
    return kSet;
}

Json::StreamWriterBuilder compactBuilder()
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    return builder;
}

std::vector<std::string> stringArrayFromJson( const Json::Value &doc, const char *key )
{
    std::vector<std::string> out;
    const Json::Value &array = doc[key];
    if ( array.isArray() )
    {
        for ( const Json::Value &entry : array )
        {
            if ( entry.isString() )
                out.push_back( entry.asString() );
        }
    }
    return out;
}

} // namespace

bool isKnownRiskClass( const std::string &riskClass )
{
    return riskClasses().count( riskClass ) > 0;
}

bool isKnownActionKind( const std::string &kind )
{
    return actionKinds().count( kind ) > 0;
}

bool isKnownPlanStatus( const std::string &status )
{
    return planStatuses().count( status ) > 0;
}

std::string jsonToString( const Json::Value &doc )
{
    return Json::writeString( compactBuilder(), doc );
}

Json::Value repairActionToJson( const RepairAction &action )
{
    Json::Value doc( Json::objectValue );
    doc["id"] = action.id;
    doc["rule_id"] = action.ruleId;
    doc["kind"] = action.kind;
    if ( !action.operatorId.empty() )
        doc["operator_id"] = action.operatorId;
    if ( !action.actionKey.empty() )
        doc["action_key"] = action.actionKey;
    doc["params"] = action.params;
    doc["risk_class"] = action.riskClass;
    doc["before_state"] = action.beforeState;
    doc["after_state"] = action.afterState;

    Json::Value informationLoss( Json::arrayValue );
    for ( const std::string &loss : action.informationLoss )
        informationLoss.append( loss );
    doc["information_loss"] = informationLoss;

    Json::Value assumptions( Json::arrayValue );
    for ( const std::string &assumption : action.assumptions )
        assumptions.append( assumption );
    doc["assumptions"] = assumptions;

    Json::Value cost( Json::objectValue );
    cost["rank"] = action.cost.rank;
    if ( !action.cost.costClass.empty() )
        cost["cost_class"] = action.cost.costClass;
    if ( !action.cost.notes.empty() )
        cost["notes"] = action.cost.notes;
    doc["cost"] = cost;

    Json::Value risk( Json::objectValue );
    risk["risk_class"] = action.risk.riskClass;
    risk["severity"] = action.risk.severity;
    risk["irreversible"] = action.risk.irreversible;
    if ( !action.risk.notes.empty() )
        risk["notes"] = action.risk.notes;
    doc["risk"] = risk;

    doc["facts_sufficient"] = action.factsSufficient;
    if ( !action.refusalCause.empty() )
        doc["refusal_cause"] = action.refusalCause;
    doc["source_finding"] = action.sourceFinding;
    doc["facts_used"] = action.factsUsed;
    doc["missing_facts"] = action.missingFacts;
    return doc;
}

bool repairActionFromJson( const Json::Value &doc, RepairAction &action, RepairError &error )
{
    if ( !doc.isObject() )
    {
        error = RepairError{ "invalid_action", "action document must be an object" };
        return false;
    }
    action = RepairAction{};
    action.id = doc.get( "id", "" ).asString();
    action.ruleId = doc.get( "rule_id", "" ).asString();
    action.kind = doc.get( "kind", "" ).asString();
    action.operatorId = doc.get( "operator_id", "" ).asString();
    action.actionKey = doc.get( "action_key", "" ).asString();
    action.params = doc.get( "params", Json::Value( Json::objectValue ) );
    action.riskClass = doc.get( "risk_class", "" ).asString();
    action.beforeState = doc.get( "before_state", Json::Value( Json::objectValue ) );
    action.afterState = doc.get( "after_state", Json::Value( Json::objectValue ) );
    action.informationLoss = stringArrayFromJson( doc, "information_loss" );
    action.assumptions = stringArrayFromJson( doc, "assumptions" );
    const Json::Value &cost = doc["cost"];
    action.cost.rank = cost.isObject() ? cost.get( "rank", 0 ).asInt() : 0;
    action.cost.costClass = cost.isObject() ? cost.get( "cost_class", "" ).asString() : "";
    action.cost.notes = cost.isObject() ? cost.get( "notes", "" ).asString() : "";
    const Json::Value &risk = doc["risk"];
    action.risk.riskClass = risk.isObject() ? risk.get( "risk_class", "" ).asString() : "";
    action.risk.severity = risk.isObject() ? risk.get( "severity", "" ).asString() : "";
    action.risk.irreversible = risk.isObject() ? risk.get( "irreversible", false ).asBool() : false;
    action.risk.notes = risk.isObject() ? risk.get( "notes", "" ).asString() : "";
    action.factsSufficient = doc.get( "facts_sufficient", true ).asBool();
    action.refusalCause = doc.get( "refusal_cause", "" ).asString();
    action.sourceFinding = doc.get( "source_finding", Json::Value( Json::objectValue ) );
    action.factsUsed = doc.get( "facts_used", Json::Value( Json::objectValue ) );
    action.missingFacts = doc.get( "missing_facts", Json::Value( Json::arrayValue ) );
    return validateRepairAction( action, error );
}

bool validateRepairAction( const RepairAction &action, RepairError &error )
{
    if ( action.id.empty() )
    {
        error = RepairError{ "invalid_action", "action id must not be empty" };
        return false;
    }
    if ( !isKnownActionKind( action.kind ) )
    {
        error = RepairError{ "invalid_action", "unknown action kind: " + action.kind };
        return false;
    }
    if ( action.kind == action_kind::kCapabilityRef && action.operatorId.empty() )
    {
        error = RepairError{ "invalid_action",
                             "capability_ref actions must name a registered operator" };
        return false;
    }
    if ( !isKnownRiskClass( action.riskClass ) )
    {
        error = RepairError{ "invalid_action", "unknown risk class: " + action.riskClass };
        return false;
    }
    if ( !isKnownRiskClass( action.risk.riskClass ) )
    {
        error = RepairError{ "invalid_action",
                             "unknown risk block class: " + action.risk.riskClass };
        return false;
    }
    if ( !riskSeverities().count( action.risk.severity ) )
    {
        error = RepairError{ "invalid_action", "unknown risk severity: " + action.risk.severity };
        return false;
    }
    // Cost convention: 1..9 for offerable candidates; 0 legal only on a
    // documented refusal (nothing to schedule -> nothing to cost).
    const bool isRefusal = !action.refusalCause.empty();
    if ( isRefusal )
    {
        if ( action.cost.rank != 0 && ( action.cost.rank < 1 || action.cost.rank > 9 ) )
        {
            error = RepairError{ "invalid_action", "cost rank must be 0 (unset) or 1..9" };
            return false;
        }
    }
    else if ( action.cost.rank < 1 || action.cost.rank > 9 )
    {
        error = RepairError{ "invalid_action", "offerable candidates need a cost rank of 1..9" };
        return false;
    }
    if ( !action.factsSufficient && action.missingFacts.empty() )
    {
        error = RepairError{ "invalid_action",
                             "insufficient-facts candidates must list their missing facts" };
        return false;
    }
    return true;
}

namespace {

/// Canonical scientific content for fingerprinting: everything except the
/// assigned identity. Mirrors the agent_plan planFingerprint discipline
/// (content addressing; identity, not science, is excluded).
Json::Value fingerprintContent( const RepairPlan &plan )
{
    Json::Value content( Json::objectValue );
    content["intent"] = plan.intent;
    content["subject"] = plan.subject;
    content["status"] = plan.status;
    content["resolves_all_blockers"] = plan.resolvesAllBlockers;

    Json::Value selected( Json::arrayValue );
    for ( const RepairAction &action : plan.selected )
    {
        Json::Value entry = repairActionToJson( action );
        entry.removeMember( "id" ); // ids encode selection order; the ordered array already carries it
        selected.append( entry );
    }
    content["selected"] = selected;

    Json::Value unresolved( Json::arrayValue );
    for ( const Json::Value &entry : plan.unresolved )
        unresolved.append( entry );
    content["unresolved"] = unresolved;

    content["requirements"] = plan.requirements;
    content["alternatives"] = plan.alternatives;
    content["policy"] = plan.policy;
    content["bounds"] = plan.bounds;
    content["provenance"] = plan.provenance;
    if ( plan.noSafeRepair.isObject() )
        content["no_safe_repair"] = plan.noSafeRepair;
    return content;
}

} // namespace

std::string repairPlanFingerprint( const RepairPlan &plan )
{
    const std::string serialized = jsonToString( fingerprintContent( plan ) );
    std::string hex = sha256Hex( serialized );
    hex.resize( 16 );
    return hex;
}

std::string assignRepairPlanIdentity( RepairPlan &plan )
{
    plan.planId = "srp-" + repairPlanFingerprint( plan );
    return plan.planId;
}

Json::Value repairPlanToJson( const RepairPlan &plan )
{
    Json::Value doc( Json::objectValue );
    doc["kind"] = kKind;
    doc["schema_version"] = kSchemaVersion;
    doc["plan_id"] = plan.planId;
    doc["intent"] = plan.intent;
    doc["subject"] = plan.subject;
    doc["status"] = plan.status;
    doc["resolves_all_blockers"] = plan.resolvesAllBlockers;

    Json::Value selected( Json::arrayValue );
    for ( const RepairAction &action : plan.selected )
        selected.append( repairActionToJson( action ) );
    doc["selected"] = selected;

    Json::Value unresolved( Json::arrayValue );
    for ( const Json::Value &entry : plan.unresolved )
        unresolved.append( entry );
    doc["unresolved"] = unresolved;

    doc["requirements"] = plan.requirements;
    doc["alternatives"] = plan.alternatives;
    doc["policy"] = plan.policy;
    doc["bounds"] = plan.bounds;
    doc["provenance"] = plan.provenance;
    if ( plan.noSafeRepair.isObject() )
        doc["no_safe_repair"] = plan.noSafeRepair;
    return doc;
}

bool readRepairPlan( const Json::Value &doc, RepairPlan &plan, RepairError &error )
{
    if ( !doc.isObject() )
    {
        error = RepairError{ "invalid_document", "repair plan must be a JSON object" };
        return false;
    }
    const std::string kind = doc.get( "kind", "" ).asString();
    if ( kind != kKind )
    {
        error = RepairError{ "invalid_document",
                             std::string( "envelope kind must be '" ) + kKind + "'" };
        return false;
    }
    const std::string version = doc.get( "schema_version", "" ).asString();
    if ( version != kSchemaVersion )
    {
        error = RepairError{ "unsupported_version",
                             "unsupported repair_plan schema version: " + version };
        return false;
    }
    plan = RepairPlan{};
    plan.planId = doc.get( "plan_id", "" ).asString();
    plan.intent = doc.get( "intent", "" ).asString();
    plan.subject = doc.get( "subject", "" ).asString();
    plan.status = doc.get( "status", "" ).asString();
    plan.resolvesAllBlockers = doc.get( "resolves_all_blockers", false ).asBool();

    const Json::Value &selected = doc["selected"];
    if ( selected.isArray() )
    {
        for ( const Json::Value &entry : selected )
        {
            RepairAction action;
            if ( !repairActionFromJson( entry, action, error ) )
                return false;
            plan.selected.push_back( action );
        }
    }
    const Json::Value &unresolved = doc["unresolved"];
    if ( unresolved.isArray() )
    {
        for ( const Json::Value &entry : unresolved )
            plan.unresolved.push_back( entry );
    }
    plan.requirements = doc.get( "requirements", Json::Value( Json::arrayValue ) );
    plan.alternatives = doc.get( "alternatives", Json::Value( Json::arrayValue ) );
    plan.policy = doc.get( "policy", Json::Value( Json::objectValue ) );
    plan.bounds = doc.get( "bounds", Json::Value( Json::objectValue ) );
    plan.provenance = doc.get( "provenance", Json::Value( Json::objectValue ) );
    if ( doc.isMember( "no_safe_repair" ) && doc["no_safe_repair"].isObject() )
        plan.noSafeRepair = doc["no_safe_repair"];
    return true;
}

} // namespace sicnu::repair

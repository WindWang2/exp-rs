// report.cpp — see report.h for why the roll-up has two levels.

#include "verification/report.h"

#include "verification/canonical_json.h"
#include "verification/digest.h"
#include "verification/spec.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sicnu::verification
{

namespace
{

void addUniqueSorted( std::vector<std::string> &target, const std::string &value )
{
    if ( value.empty() )
    {
        return;
    }
    target.push_back( value );
    std::sort( target.begin(), target.end() );
    target.erase( std::unique( target.begin(), target.end() ), target.end() );
}

CheckStatus statusOf( const std::vector<CheckResult> &results, const std::string &checkId,
                      CheckStatus fallback )
{
    for ( const CheckResult &result : results )
    {
        if ( result.checkId == checkId )
        {
            return result.status;
        }
    }
    return fallback;
}

std::vector<ReplanClass> replanClassesFor( const std::vector<std::string> &codes )
{
    std::vector<ReplanClass> classes;
    for ( const std::string &code : codes )
    {
        const ReplanClass candidate = replanClassForCode( code );
        if ( std::find( classes.begin(), classes.end(), candidate ) == classes.end() )
        {
            classes.push_back( candidate );
        }
    }
    std::sort( classes.begin(), classes.end(), []( ReplanClass left, ReplanClass right )
               { return static_cast<int>( left ) < static_cast<int>( right ); } );
    return classes;
}

Json::Value stringArray( const std::vector<std::string> &values )
{
    Json::Value array{ Json::arrayValue };
    for ( const std::string &value : values )
    {
        array.append( value );
    }
    return array;
}

bool readStringArray( const Json::Value &json, const char *name, std::vector<std::string> &out,
                      std::string &error )
{
    out.clear();
    if ( !json.isMember( name ) )
    {
        return true;
    }
    if ( !json[name].isArray() )
    {
        error = std::string( "member '" ) + name + "' must be an array";
        return false;
    }
    for ( const Json::Value &entry : json[name] )
    {
        if ( !entry.isString() )
        {
            error = std::string( "every entry of '" ) + name + "' must be a string";
            return false;
        }
        out.emplace_back( entry.asString() );
    }
    return true;
}

} // namespace

Json::Value NodeOutcome::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["node_id"] = nodeId;
    json["status"] = statusToWire( status );
    json["check_ids"] = stringArray( checkIds );
    json["failure_codes"] = stringArray( failureCodes );
    json["promoted_check_ids"] = stringArray( promotedCheckIds );
    return json;
}

bool NodeOutcome::fromJson( const Json::Value &json, NodeOutcome &out, std::string &error )
{
    if ( !json.isObject() )
    {
        error = "a node outcome must be an object";
        return false;
    }
    NodeOutcome loaded;
    if ( json.isMember( "node_id" ) )
    {
        if ( !json["node_id"].isString() )
        {
            error = "member 'node_id' must be a string";
            return false;
        }
        loaded.nodeId = json["node_id"].asString();
    }
    if ( json.isMember( "status" ) )
    {
        if ( !json["status"].isString() ||
             !statusFromWire( json["status"].asString(), loaded.status ) )
        {
            error = "member 'status' must be one of pass|fail|indeterminate";
            return false;
        }
    }
    if ( !readStringArray( json, "check_ids", loaded.checkIds, error ) ||
         !readStringArray( json, "failure_codes", loaded.failureCodes, error ) ||
         !readStringArray( json, "promoted_check_ids", loaded.promotedCheckIds, error ) )
    {
        return false;
    }
    out = std::move( loaded );
    return true;
}

Json::Value TaskOutcome::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["status"] = statusToWire( status );
    json["blocking_nodes"] = stringArray( blockingNodes );
    json["first_blocking_node"] = firstBlockingNode;
    json["failure_codes"] = stringArray( failureCodes );

    Json::Value replanArray{ Json::arrayValue };
    for ( ReplanClass replanClass : replanClasses )
    {
        replanArray.append( replanClassToWire( replanClass ) );
    }
    json["replan_classes"] = replanArray;
    return json;
}

bool TaskOutcome::fromJson( const Json::Value &json, TaskOutcome &out, std::string &error )
{
    if ( !json.isObject() )
    {
        error = "a task outcome must be an object";
        return false;
    }
    TaskOutcome loaded;
    if ( json.isMember( "status" ) )
    {
        if ( !json["status"].isString() ||
             !statusFromWire( json["status"].asString(), loaded.status ) )
        {
            error = "member 'status' must be one of pass|fail|indeterminate";
            return false;
        }
    }
    if ( !readStringArray( json, "blocking_nodes", loaded.blockingNodes, error ) ||
         !readStringArray( json, "failure_codes", loaded.failureCodes, error ) )
    {
        return false;
    }
    if ( json.isMember( "first_blocking_node" ) )
    {
        if ( !json["first_blocking_node"].isString() )
        {
            error = "member 'first_blocking_node' must be a string";
            return false;
        }
        loaded.firstBlockingNode = json["first_blocking_node"].asString();
    }
    if ( json.isMember( "replan_classes" ) )
    {
        if ( !json["replan_classes"].isArray() )
        {
            error = "member 'replan_classes' must be an array";
            return false;
        }
        std::set<ReplanClass> seen;
        for ( const Json::Value &entry : json["replan_classes"] )
        {
            if ( !entry.isString() )
            {
                error = "every entry of 'replan_classes' must be a string";
                return false;
            }
            const std::string wire = entry.asString();
            ReplanClass parsed = ReplanClass::None;
            if ( wire == "none" )
            {
                parsed = ReplanClass::None;
            }
            else if ( wire == "retry" )
            {
                parsed = ReplanClass::Retry;
            }
            else if ( wire == "replan" )
            {
                parsed = ReplanClass::Replan;
            }
            else if ( wire == "abort" )
            {
                parsed = ReplanClass::Abort;
            }
            else
            {
                error = "unknown replan class '" + wire + "'";
                return false;
            }
            if ( seen.insert( parsed ).second )
            {
                loaded.replanClasses.push_back( parsed );
            }
        }
    }
    out = std::move( loaded );
    return true;
}

Json::Value BudgetUsage::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["checks_evaluated"] = static_cast<Json::Int64>( checksEvaluated );
    json["checks_unevaluated"] = static_cast<Json::Int64>( checksUnevaluated );
    json["nodes"] = static_cast<Json::Int64>( nodes );
    json["max_evidence_bytes"] = static_cast<Json::Int64>( maxEvidenceBytes );
    json["max_depth_seen"] = maxDepthSeen;
    json["exceeded"] = exceeded;
    json["reasons"] = stringArray( reasons );
    return json;
}

bool BudgetUsage::fromJson( const Json::Value &json, BudgetUsage &out, std::string &error )
{
    if ( !json.isObject() )
    {
        error = "budget usage must be an object";
        return false;
    }
    BudgetUsage loaded;
    auto readCount = [ &json, &error ]( const char *name, std::size_t &target )
    {
        if ( !json.isMember( name ) )
        {
            return true;
        }
        if ( !json[name].isIntegral() )
        {
            error = std::string( "member '" ) + name + "' must be a non-negative integer";
            return false;
        }
        target = static_cast<std::size_t>( json[name].asInt64() );
        return true;
    };
    if ( !readCount( "checks_evaluated", loaded.checksEvaluated ) ||
         !readCount( "checks_unevaluated", loaded.checksUnevaluated ) ||
         !readCount( "nodes", loaded.nodes ) ||
         !readCount( "max_evidence_bytes", loaded.maxEvidenceBytes ) )
    {
        return false;
    }
    if ( json.isMember( "max_depth_seen" ) )
    {
        if ( !json["max_depth_seen"].isIntegral() )
        {
            error = "member 'max_depth_seen' must be an integer";
            return false;
        }
        loaded.maxDepthSeen = json["max_depth_seen"].asInt();
    }
    if ( json.isMember( "exceeded" ) )
    {
        if ( !json["exceeded"].isBool() )
        {
            error = "member 'exceeded' must be a boolean";
            return false;
        }
        loaded.exceeded = json["exceeded"].asBool();
    }
    if ( !readStringArray( json, "reasons", loaded.reasons, error ) )
    {
        return false;
    }
    out = std::move( loaded );
    return true;
}

NodeOutcome rollUpNode( const std::string &nodeId, const std::vector<std::string> &checkIds,
                        const std::vector<CheckResult> &results, IndeterminatePolicy policy )
{
    NodeOutcome outcome;
    outcome.nodeId = nodeId;
    outcome.checkIds = checkIds;

    if ( checkIds.empty() )
    {
        // A node whose postcondition nobody expressed is not a satisfied node.
        outcome.status = CheckStatus::Indeterminate;
        return outcome;
    }

    std::vector<std::pair<std::string, CheckStatus>> labelled;
    labelled.reserve( checkIds.size() );
    for ( const std::string &checkId : checkIds )
    {
        // A check we have no result for is treated as Indeterminate, not as
        // absent: it was declared, so it was meant to be answered.
        labelled.emplace_back( checkId, statusOf( results, checkId, CheckStatus::Indeterminate ) );
    }

    const RollupResult rolled = rollUp( labelled, policy );
    outcome.status = rolled.status;
    outcome.promotedCheckIds = rolled.promoted;

    for ( const auto &entry : labelled )
    {
        if ( entry.second == CheckStatus::Pass )
        {
            continue;
        }
        for ( const CheckResult &result : results )
        {
            if ( result.checkId != entry.first )
            {
                continue;
            }
            addUniqueSorted( outcome.failureCodes, result.failureCode );
            break;
        }
    }

    return outcome;
}

TaskOutcome rollUpTask( const std::vector<NodeOutcome> &nodes )
{
    TaskOutcome task;
    if ( nodes.empty() )
    {
        // Zero nodes is zero evidence. Deliberately NO blocking node: there is
        // nothing to point at, and inventing a blocker would be the same genre
        // of dishonesty as inventing a pass.
        task.status = CheckStatus::Indeterminate;
        return task;
    }

    CheckStatus accumulated = nodes.front().status;
    for ( const NodeOutcome &node : nodes )
    {
        accumulated = combineStatus( accumulated, node.status );
        if ( node.status != CheckStatus::Pass )
        {
            task.blockingNodes.push_back( node.nodeId );
        }
        for ( const std::string &code : node.failureCodes )
        {
            addUniqueSorted( task.failureCodes, code );
        }
    }

    task.status = accumulated;
    task.firstBlockingNode = task.blockingNodes.empty() ? std::string() : task.blockingNodes.front();
    task.replanClasses = replanClassesFor( task.failureCodes );
    return task;
}

Json::Value VerificationReport::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["schema"] = kVerificationReportSchema;
    json["schema_version"] = schemaVersion;
    json["spec_id"] = specId;
    json["spec_digest"] = specDigest;
    json["status"] = statusToWire( status );

    Json::Value resultArray{ Json::arrayValue };
    for ( const CheckResult &result : results )
    {
        resultArray.append( result.toJson() );
    }
    json["results"] = resultArray;

    Json::Value nodeArray{ Json::arrayValue };
    for ( const NodeOutcome &node : nodes )
    {
        nodeArray.append( node.toJson() );
    }
    json["nodes"] = nodeArray;

    json["outcome"] = outcome.toJson();
    json["budget_usage"] = budgetUsage.toJson();
    json["counters"] = counters.isObject() ? counters : Json::Value{ Json::objectValue };
    return json;
}

bool VerificationReport::fromJson( const Json::Value &json, VerificationReport &out,
                                   std::string &error )
{
    if ( !json.isObject() )
    {
        error = "a verification report must be an object";
        return false;
    }

    VerificationReport loaded;
    if ( json.isMember( "schema" ) )
    {
        if ( !json["schema"].isString() || json["schema"].asString() != kVerificationReportSchema )
        {
            error = "unsupported report schema (expected " +
                    std::string( kVerificationReportSchema ) + ")";
            return false;
        }
    }
    if ( json.isMember( "schema_version" ) )
    {
        if ( !json["schema_version"].isIntegral() )
        {
            error = "member 'schema_version' must be an integer";
            return false;
        }
        loaded.schemaVersion = json["schema_version"].asInt();
    }
    if ( json.isMember( "spec_id" ) )
    {
        if ( !json["spec_id"].isString() )
        {
            error = "member 'spec_id' must be a string";
            return false;
        }
        loaded.specId = json["spec_id"].asString();
    }
    if ( json.isMember( "spec_digest" ) )
    {
        if ( !json["spec_digest"].isString() )
        {
            error = "member 'spec_digest' must be a string";
            return false;
        }
        loaded.specDigest = json["spec_digest"].asString();
    }
    if ( json.isMember( "status" ) )
    {
        if ( !json["status"].isString() ||
             !statusFromWire( json["status"].asString(), loaded.status ) )
        {
            error = "member 'status' must be one of pass|fail|indeterminate";
            return false;
        }
    }
    if ( json.isMember( "results" ) )
    {
        if ( !json["results"].isArray() )
        {
            error = "member 'results' must be an array";
            return false;
        }
        for ( const Json::Value &entry : json["results"] )
        {
            CheckResult result;
            if ( !CheckResult::fromJson( entry, result, error ) )
            {
                return false;
            }
            loaded.results.push_back( std::move( result ) );
        }
    }
    if ( json.isMember( "nodes" ) )
    {
        if ( !json["nodes"].isArray() )
        {
            error = "member 'nodes' must be an array";
            return false;
        }
        for ( const Json::Value &entry : json["nodes"] )
        {
            NodeOutcome node;
            if ( !NodeOutcome::fromJson( entry, node, error ) )
            {
                return false;
            }
            loaded.nodes.push_back( std::move( node ) );
        }
    }
    if ( json.isMember( "outcome" ) && !TaskOutcome::fromJson( json["outcome"], loaded.outcome, error ) )
    {
        return false;
    }
    if ( json.isMember( "budget_usage" ) &&
         !BudgetUsage::fromJson( json["budget_usage"], loaded.budgetUsage, error ) )
    {
        return false;
    }
    if ( json.isMember( "counters" ) )
    {
        if ( !json["counters"].isObject() )
        {
            error = "member 'counters' must be an object";
            return false;
        }
        loaded.counters = json["counters"];
    }

    out = std::move( loaded );
    return true;
}

VerificationReport buildReport( const VerificationSpec &spec,
                                const std::vector<CheckResult> &results,
                                const NodeCheckMap &nodeChecks,
                                IndeterminatePolicy policy,
                                const BudgetUsage &usage )
{
    VerificationReport report;
    report.specId = spec.specId;
    report.specDigest = specDigest( spec );
    report.results = results;
    report.budgetUsage = usage;
    report.budgetUsage.nodes = nodeChecks.size();

    for ( const auto &entry : nodeChecks )
    {
        report.nodes.push_back( rollUpNode( entry.first, entry.second, results, policy ) );
    }
    report.outcome = rollUpTask( report.nodes );
    report.status = report.outcome.status;

    Json::Value byStatus{ Json::objectValue };
    byStatus["pass"] = 0;
    byStatus["fail"] = 0;
    byStatus["indeterminate"] = 0;

    Json::Value byKind{ Json::objectValue };
    for ( const CheckResult &result : results )
    {
        byStatus[ statusToWire( result.status ) ] = byStatus[ statusToWire( result.status ) ].asInt() + 1;
        if ( byKind.isMember( result.kind ) )
        {
            byKind[ result.kind ] = byKind[ result.kind ].asInt() + 1;
        }
        else
        {
            byKind[ result.kind ] = 1;
        }
    }

    report.counters = Json::Value{ Json::objectValue };
    report.counters["by_status"] = byStatus;
    report.counters["by_kind"] = byKind;
    report.counters["nodes"] = static_cast<Json::Int64>( report.nodes.size() );
    report.counters["checks"] = static_cast<Json::Int64>( results.size() );

    return report;
}

std::string reportDigest( const VerificationReport &report )
{
    std::string error;
    return canonicalDigestSha256( report.toJson(), error );
}

} // namespace sicnu::verification

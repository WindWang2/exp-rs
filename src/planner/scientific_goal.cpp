// src/planner/scientific_goal.cpp
#include "planner/scientific_goal.h"

#include "planner/json_util.h"

namespace sicnu::planner {

namespace {
using json_util::error;
constexpr size_t kIdMax = PlanLimits::kMaxIdChars;
constexpr size_t kTextMax = PlanLimits::kMaxTextChars;

bool readTemporalScope( const Json::Value &parent, std::optional<GoalTemporalScope> &out,
                        std::string &err )
{
    out.reset();
    if ( !parent.isMember( "temporal_scope" ) || parent["temporal_scope"].isNull() )
        return true;
    const Json::Value &scope = parent["temporal_scope"];
    if ( !scope.isObject() )
    {
        err = json_util::error( "invalid_field", "temporal_scope must be an object" );
        return false;
    }
    GoalTemporalScope parsed;
    if ( !json_util::readOptionalBoundedString( scope, "start", kTextMax, parsed.start, err ) )
        return false;
    if ( !json_util::readOptionalBoundedString( scope, "end", kTextMax, parsed.end, err ) )
        return false;
    if ( scope.isMember( "min_scenes" ) )
    {
        if ( !scope["min_scenes"].isInt() || scope["min_scenes"].asInt() < 0 )
        {
            err = json_util::error( "invalid_field", "temporal_scope.min_scenes must be a non-negative int" );
            return false;
        }
        parsed.minScenes = scope["min_scenes"].asInt();
    }
    if ( scope.isMember( "max_gap_days" ) )
    {
        if ( !scope["max_gap_days"].isInt() || scope["max_gap_days"].asInt() < 0 )
        {
            err = json_util::error( "invalid_field",
                         "temporal_scope.max_gap_days must be a non-negative int" );
            return false;
        }
        parsed.maxGapDays = scope["max_gap_days"].asInt();
    }
    out = parsed;
    return true;
}

bool readAcceptanceCriteria( const Json::Value &doc, ScientificGoal &out, std::string &err )
{
    if ( !doc.isMember( "acceptance_criteria" ) )
        return true; // additive-optional: absent = empty (plan.md §4.1)
    const Json::Value &criteria = doc["acceptance_criteria"];
    if ( !criteria.isArray() )
    {
        err = json_util::error( "invalid_field", "acceptance_criteria must be an array" );
        return false;
    }
    if ( static_cast<int>( criteria.size() ) > PlanLimits::kMaxAcceptanceCriteria )
    {
        err = json_util::error( "out_of_bounds", "acceptance_criteria over "
                                          + std::to_string( PlanLimits::kMaxAcceptanceCriteria )
                                          + " entries" );
        return false;
    }
    for ( const auto &entry : criteria )
    {
        if ( !entry.isObject() )
        {
            err = json_util::error( "invalid_field", "acceptance_criteria entry must be an object" );
            return false;
        }
        GoalAcceptanceCriterion criterion;
        if ( !json_util::readBoundedString( entry, "criterion_id", kIdMax, criterion.criterionId,
                                            err ) )
            return false;
        if ( !json_util::readBoundedString( entry, "check", kTextMax, criterion.check, err ) )
            return false;
        if ( !json_util::readOptionalBoundedString( entry, "target", kTextMax, criterion.target,
                                                    err ) )
            return false;
        out.acceptanceCriteria.push_back( criterion );
    }
    return true;
}
} // namespace

Json::Value scientificGoalToJson( const ScientificGoal &goal )
{
    Json::Value doc( Json::objectValue );
    doc["kind"] = kGoalEnvelopeKind;
    doc["schema_version"] = kSchemaVersion;
    doc["goal_id"] = goal.goalId;
    doc["goal_kind"] = goal.kind;
    doc["subject"] = goal.subject;
    if ( !goal.quantity.empty() )
        doc["quantity"] = goal.quantity;
    if ( goal.temporalScope )
    {
        Json::Value scope( Json::objectValue );
        if ( !goal.temporalScope->start.empty() )
            scope["start"] = goal.temporalScope->start;
        if ( !goal.temporalScope->end.empty() )
            scope["end"] = goal.temporalScope->end;
        if ( goal.temporalScope->minScenes > 0 )
            scope["min_scenes"] = goal.temporalScope->minScenes;
        if ( goal.temporalScope->maxGapDays > 0 )
            scope["max_gap_days"] = goal.temporalScope->maxGapDays;
        doc["temporal_scope"] = scope;
    }
    if ( !goal.acceptanceCriteria.empty() )
    {
        Json::Value criteria( Json::arrayValue );
        for ( const auto &criterion : goal.acceptanceCriteria )
        {
            Json::Value entry( Json::objectValue );
            entry["criterion_id"] = criterion.criterionId;
            entry["check"] = criterion.check;
            if ( !criterion.target.empty() )
                entry["target"] = criterion.target;
            criteria.append( entry );
        }
        doc["acceptance_criteria"] = criteria;
    }
    return doc;
}

bool scientificGoalFromJson( const Json::Value &doc, ScientificGoal &out, std::string &error )
{
    out = ScientificGoal{};
    if ( !json_util::checkEnvelope( doc, kGoalEnvelopeKind, error ) )
        return false;
    if ( !json_util::readBoundedString( doc, "goal_id", kIdMax, out.goalId, error ) )
        return false;
    if ( !json_util::readBoundedString( doc, "goal_kind", kIdMax, out.kind, error ) )
        return false;
    if ( !isKnownGoalKind( out.kind ) )
    {
        error = json_util::error( "invalid_field", "unknown goal kind \"" + out.kind + "\"" );
        return false;
    }
    if ( !json_util::readBoundedString( doc, "subject", kTextMax, out.subject, error ) )
        return false;
    if ( !json_util::readOptionalBoundedString( doc, "quantity", kTextMax, out.quantity, error ) )
        return false;
    if ( !readTemporalScope( doc, out.temporalScope, error ) )
        return false;
    if ( !readAcceptanceCriteria( doc, out, error ) )
        return false;
    return true;
}

std::vector<std::string> validateScientificGoal( const ScientificGoal &goal )
{
    std::vector<std::string> problems;
    if ( goal.goalId.empty() || goal.goalId.size() > PlanLimits::kMaxIdChars )
        problems.push_back( "out_of_bounds: goal_id empty or over 64 chars" );
    if ( !isKnownGoalKind( goal.kind ) )
        problems.push_back( "invalid_field: unknown goal kind \"" + goal.kind + "\"" );
    if ( goal.subject.empty() || goal.subject.size() > PlanLimits::kMaxTextChars )
        problems.push_back( "out_of_bounds: subject empty or over 512 chars" );
    if ( goal.acceptanceCriteria.size() > size_t( PlanLimits::kMaxAcceptanceCriteria ) )
        problems.push_back( "out_of_bounds: too many acceptance criteria" );
    return problems;
}

} // namespace sicnu::planner

// src/planner/scientific_plan.cpp
#include "planner/scientific_plan.h"

#include "planner/json_util.h"
#include "planner/sha256_util.h"

#include <set>

namespace sicnu::planner {

const std::vector<std::string> kPreconditionKinds = {
    "asset_state", "numeric_domain", "min_scene_count", "grid", "band_role",
};

namespace {
using json_util::error;
constexpr size_t kIdMax = PlanLimits::kMaxIdChars;
constexpr size_t kTextMax = PlanLimits::kMaxTextChars;
constexpr size_t kParamsMaxChars = 8192;

bool boundedStringList( const Json::Value &array, size_t maxChars,
                        std::vector<std::string> &out, std::string &err, const char *what )
{
    for ( const auto &entry : array )
    {
        if ( !entry.isString() || entry.asString().empty() || entry.asString().size() > maxChars )
        {
            err = json_util::error( "invalid_field",
                         std::string( what ) + " entries must be non-empty bounded strings" );
            return false;
        }
        out.push_back( entry.asString() );
    }
    return true;
}

Json::Value stringListToJson( const std::vector<std::string> &values )
{
    Json::Value array( Json::arrayValue );
    for ( const auto &value : values )
        array.append( value );
    return array;
}

bool readInputs( const Json::Value &stepJson, PlannerStep &step, std::string &err )
{
    if ( !stepJson.isMember( "inputs" ) )
        return true;
    const Json::Value &inputs = stepJson["inputs"];
    if ( !inputs.isArray() )
    {
        err = json_util::error( "invalid_field", "steps[].inputs must be an array" );
        return false;
    }
    if ( static_cast<int>( inputs.size() ) > PlanLimits::kMaxInputsPerStep )
    {
        err = json_util::error( "out_of_bounds", "steps[].inputs over "
                                          + std::to_string( PlanLimits::kMaxInputsPerStep ) );
        return false;
    }
    for ( const auto &input : inputs )
    {
        if ( !input.isObject() )
        {
            err = json_util::error( "invalid_field", "steps[].inputs[] must be objects" );
            return false;
        }
        StepInput parsed;
        if ( !json_util::readOptionalBoundedString( input, "from_step_id", kIdMax,
                                                    parsed.fromStepId, err ) )
            return false;
        if ( !json_util::readOptionalBoundedString( input, "asset_ref", kIdMax, parsed.assetRef,
                                                    err ) )
            return false;
        const bool stepForm = !parsed.fromStepId.empty();
        const bool assetForm = !parsed.assetRef.empty();
        if ( stepForm == assetForm )
        {
            err = json_util::error( "invalid_field",
                         "steps[].inputs[] must carry exactly one of from_step_id/asset_ref" );
            return false;
        }
        if ( !json_util::readBoundedString( input, "as", kIdMax, parsed.as, err ) )
            return false;
        step.inputs.push_back( parsed );
    }
    return true;
}

bool readPreconditions( const Json::Value &stepJson, PlannerStep &step, std::string &err )
{
    if ( !stepJson.isMember( "preconditions" ) )
        return true;
    const Json::Value &preconditions = stepJson["preconditions"];
    if ( !preconditions.isArray() )
    {
        err = json_util::error( "invalid_field", "steps[].preconditions must be an array" );
        return false;
    }
    for ( const auto &entry : preconditions )
    {
        if ( !entry.isObject() )
        {
            err = json_util::error( "invalid_field", "steps[].preconditions[] must be objects" );
            return false;
        }
        StepPrecondition parsed;
        if ( !json_util::readBoundedString( entry, "kind", kIdMax, parsed.kind, err ) )
            return false;
        if ( !isKnownVocabValue( kPreconditionKinds, parsed.kind ) )
        {
            err = json_util::error( "invalid_field", "unknown precondition kind \"" + parsed.kind + "\"" );
            return false;
        }
        if ( !json_util::readOptionalBoundedString( entry, "asset_ref", kIdMax, parsed.assetRef,
                                                    err ) )
            return false;
        if ( !json_util::readOptionalBoundedString( entry, "domain", kIdMax, parsed.domain, err ) )
            return false;
        if ( !parsed.domain.empty() && !isKnownContractsNumericDomain( parsed.domain ) )
        {
            err = json_util::error( "invalid_field",
                         "unknown precondition domain \"" + parsed.domain + "\"" );
            return false;
        }
        if ( entry.isMember( "min_scenes" ) )
        {
            if ( !entry["min_scenes"].isInt() || entry["min_scenes"].asInt() < 0 )
            {
                err = json_util::error( "invalid_field", "precondition min_scenes must be non-negative" );
                return false;
            }
            parsed.minScenes = entry["min_scenes"].asInt();
        }
        if ( !json_util::readOptionalBoundedString( entry, "detail", kTextMax, parsed.detail,
                                                    err ) )
            return false;
        step.preconditions.push_back( parsed );
    }
    return true;
}

bool readTransitions( const Json::Value &stepJson, PlannerStep &step, std::string &err )
{
    if ( !stepJson.isMember( "expected_transitions" ) )
        return true;
    const Json::Value &transitions = stepJson["expected_transitions"];
    if ( !transitions.isArray() )
    {
        err = json_util::error( "invalid_field", "steps[].expected_transitions must be an array" );
        return false;
    }
    for ( const auto &entry : transitions )
    {
        if ( !entry.isObject() )
        {
            err = json_util::error( "invalid_field", "steps[].expected_transitions[] must be objects" );
            return false;
        }
        ExpectedTransition parsed;
        if ( !json_util::readBoundedString( entry, "asset_ref", kIdMax, parsed.assetRef, err ) )
            return false;
        if ( !json_util::readBoundedString( entry, "from_domain", kIdMax, parsed.fromDomain,
                                            err ) )
            return false;
        if ( !json_util::readBoundedString( entry, "to_domain", kIdMax, parsed.toDomain, err ) )
            return false;
        if ( !isKnownContractsNumericDomain( parsed.fromDomain )
             || !isKnownContractsNumericDomain( parsed.toDomain ) )
        {
            err = json_util::error( "invalid_field",
                         "expected transition domains must be contracts numeric domains" );
            return false;
        }
        step.expectedTransitions.push_back( parsed );
    }
    return true;
}

bool readVerifierTargets( const Json::Value &stepJson, PlannerStep &step, std::string &err )
{
    if ( !stepJson.isMember( "verifier_targets" ) )
        return true;
    const Json::Value &targets = stepJson["verifier_targets"];
    if ( !targets.isArray() )
    {
        err = json_util::error( "invalid_field", "steps[].verifier_targets must be an array" );
        return false;
    }
    for ( const auto &entry : targets )
    {
        if ( !entry.isObject() )
        {
            err = json_util::error( "invalid_field", "steps[].verifier_targets[] must be objects" );
            return false;
        }
        VerifierTarget parsed;
        if ( !json_util::readBoundedString( entry, "check", kTextMax, parsed.check, err ) )
            return false;
        if ( !json_util::readOptionalBoundedString( entry, "target", kTextMax, parsed.target,
                                                    err ) )
            return false;
        step.verifierTargets.push_back( parsed );
    }
    return true;
}

bool readStep( const Json::Value &stepJson, PlannerStep &step, std::string &err )
{
    if ( !stepJson.isObject() )
    {
        err = json_util::error( "invalid_field", "steps[] must be objects" );
        return false;
    }
    if ( !json_util::readBoundedString( stepJson, "step_id", kIdMax, step.stepId, err ) )
        return false;
    if ( !json_util::readBoundedString( stepJson, "role", kIdMax, step.role, err ) )
        return false;
    if ( !isKnownStepRole( step.role ) )
    {
        err = json_util::error( "invalid_field", "unknown step role \"" + step.role + "\"" );
        return false;
    }
    if ( !json_util::readOptionalBoundedString( stepJson, "operator_id", kIdMax, step.operatorId,
                                                err ) )
        return false;
    if ( !json_util::readBoundedString( stepJson, "family", kIdMax, step.family, err ) )
        return false;
    if ( !isKnownFamilySlot( step.family ) )
    {
        err = json_util::error( "invalid_field", "unknown family slot \"" + step.family + "\"" );
        return false;
    }
    if ( !readInputs( stepJson, step, err ) )
        return false;
    if ( stepJson.isMember( "params" ) )
    {
        if ( !stepJson["params"].isObject() )
        {
            err = json_util::error( "invalid_field", "steps[].params must be an object" );
            return false;
        }
        step.params = stepJson["params"];
        if ( json_util::canonicalCompact( step.params ).size() > kParamsMaxChars )
        {
            err = json_util::error( "out_of_bounds", "steps[].params serialized over "
                                              + std::to_string( kParamsMaxChars ) + " chars" );
            return false;
        }
    }
    if ( !readPreconditions( stepJson, step, err ) )
        return false;
    if ( !readTransitions( stepJson, step, err ) )
        return false;
    if ( !readVerifierTargets( stepJson, step, err ) )
        return false;
    if ( !json_util::readBoundedString( stepJson, "cost_class", kIdMax, step.costClass, err ) )
        return false;
    if ( !isKnownCostClass( step.costClass ) )
    {
        err = json_util::error( "invalid_field", "unknown cost class \"" + step.costClass + "\"" );
        return false;
    }
    if ( stepJson.isMember( "estimated_ram_mb" ) )
    {
        if ( !stepJson["estimated_ram_mb"].isInt64()
             || stepJson["estimated_ram_mb"].asInt64() < 0 )
        {
            err = json_util::error( "invalid_field", "estimated_ram_mb must be non-negative" );
            return false;
        }
        step.estimatedRamMb = stepJson["estimated_ram_mb"].asInt64();
    }
    if ( stepJson.isMember( "risk_notes" ) )
    {
        if ( !stepJson["risk_notes"].isArray()
             || !boundedStringList( stepJson["risk_notes"], kTextMax, step.riskNotes, err,
                                    "steps[].risk_notes" ) )
        {
            if ( err.empty() )
                err = json_util::error( "invalid_field", "steps[].risk_notes must be an array" );
            return false;
        }
    }
    if ( stepJson.isMember( "deterministic" ) )
    {
        if ( !stepJson["deterministic"].isBool() )
        {
            err = json_util::error( "invalid_field", "steps[].deterministic must be a bool" );
            return false;
        }
        step.deterministic = stepJson["deterministic"].asBool();
    }
    if ( stepJson.isMember( "student_decision" ) )
    {
        if ( !stepJson["student_decision"].isBool() )
        {
            err = json_util::error( "invalid_field", "student_decision must be a bool" );
            return false;
        }
        step.studentDecision = stepJson["student_decision"].asBool();
    }
    return true;
}

Json::Value stepToJson( const PlannerStep &step )
{
    Json::Value entry( Json::objectValue );
    entry["step_id"] = step.stepId;
    entry["role"] = step.role;
    if ( !step.operatorId.empty() )
        entry["operator_id"] = step.operatorId;
    entry["family"] = step.family;
    if ( !step.inputs.empty() )
    {
        Json::Value inputs( Json::arrayValue );
        for ( const auto &input : step.inputs )
        {
            Json::Value wire( Json::objectValue );
            if ( !input.fromStepId.empty() )
                wire["from_step_id"] = input.fromStepId;
            else
                wire["asset_ref"] = input.assetRef;
            wire["as"] = input.as;
            inputs.append( wire );
        }
        entry["inputs"] = inputs;
    }
    if ( !step.params.empty() )
        entry["params"] = step.params;
    if ( !step.preconditions.empty() )
    {
        Json::Value preconditions( Json::arrayValue );
        for ( const auto &precondition : step.preconditions )
        {
            Json::Value wire( Json::objectValue );
            wire["kind"] = precondition.kind;
            if ( !precondition.assetRef.empty() )
                wire["asset_ref"] = precondition.assetRef;
            if ( !precondition.domain.empty() )
                wire["domain"] = precondition.domain;
            if ( precondition.minScenes > 0 )
                wire["min_scenes"] = precondition.minScenes;
            if ( !precondition.detail.empty() )
                wire["detail"] = precondition.detail;
            preconditions.append( wire );
        }
        entry["preconditions"] = preconditions;
    }
    if ( !step.expectedTransitions.empty() )
    {
        Json::Value transitions( Json::arrayValue );
        for ( const auto &transition : step.expectedTransitions )
        {
            Json::Value wire( Json::objectValue );
            wire["asset_ref"] = transition.assetRef;
            wire["from_domain"] = transition.fromDomain;
            wire["to_domain"] = transition.toDomain;
            transitions.append( wire );
        }
        entry["expected_transitions"] = transitions;
    }
    if ( !step.verifierTargets.empty() )
    {
        Json::Value targets( Json::arrayValue );
        for ( const auto &target : step.verifierTargets )
        {
            Json::Value wire( Json::objectValue );
            wire["check"] = target.check;
            if ( !target.target.empty() )
                wire["target"] = target.target;
            targets.append( wire );
        }
        entry["verifier_targets"] = targets;
    }
    entry["cost_class"] = step.costClass;
    if ( step.estimatedRamMb > 0 )
        entry["estimated_ram_mb"] = Json::Value::Int64( step.estimatedRamMb );
    if ( !step.deterministic )
        entry["deterministic"] = false;
    if ( !step.riskNotes.empty() )
        entry["risk_notes"] = stringListToJson( step.riskNotes );
    if ( step.studentDecision )
        entry["student_decision"] = true;
    return entry;
}
} // namespace

Json::Value scientificPlanToJson( const ScientificPlan &plan )
{
    Json::Value doc( Json::objectValue );
    doc["kind"] = kPlanEnvelopeKind;
    doc["schema_version"] = kSchemaVersion;
    doc["plan_id"] = plan.planId;
    doc["goal_id"] = plan.goalId;
    doc["goal_kind"] = plan.goalKind;
    doc["rules_revision"] = plan.rulesRevision;

    Json::Value mode( Json::objectValue );
    mode["kind"] = plan.modeKind;
    mode["autonomy"] = plan.autonomy;
    doc["mode"] = mode;

    doc["verdict"] = plan.verdict;
    if ( !plan.verdictReasons.empty() )
        doc["verdict_reasons"] = stringListToJson( plan.verdictReasons );

    Json::Value steps( Json::arrayValue );
    for ( const auto &step : plan.steps )
        steps.append( stepToJson( step ) );
    doc["steps"] = steps;

    if ( !plan.alternatives.empty() )
    {
        Json::Value alternatives( Json::arrayValue );
        for ( const auto &alternative : plan.alternatives )
        {
            Json::Value wire( Json::objectValue );
            wire["alternative_id"] = alternative.alternativeId;
            wire["summary"] = alternative.summary;
            if ( !alternative.whyChosen.empty() )
                wire["why_chosen"] = alternative.whyChosen;
            if ( !alternative.whyNot.empty() )
                wire["why_not"] = alternative.whyNot;
            wire["candidate_index"] = alternative.candidateIndex;
            alternatives.append( wire );
        }
        doc["alternatives"] = alternatives;
    }

    if ( !plan.openQuestions.empty() )
    {
        Json::Value questions( Json::arrayValue );
        for ( const auto &question : plan.openQuestions )
        {
            Json::Value wire( Json::objectValue );
            wire["question_id"] = question.questionId;
            wire["kind"] = question.kind;
            wire["blocking"] = question.blocking;
            wire["detail"] = question.detail;
            if ( !question.suggestion.empty() )
                wire["suggestion"] = question.suggestion;
            questions.append( wire );
        }
        doc["open_questions"] = questions;
    }

    Json::Value cost( Json::objectValue );
    cost["aggregate_cost_class"] = plan.cost.aggregateCostClass;
    if ( plan.cost.totalEstimatedRamMb > 0 )
        cost["total_estimated_ram_mb"] = Json::Value::Int64( plan.cost.totalEstimatedRamMb );
    doc["cost"] = cost;

    if ( !plan.risks.empty() )
    {
        Json::Value risks( Json::arrayValue );
        for ( const auto &risk : plan.risks )
        {
            Json::Value wire( Json::objectValue );
            wire["kind"] = risk.kind;
            wire["detail"] = risk.detail;
            if ( !risk.stepId.empty() )
                wire["step_id"] = risk.stepId;
            risks.append( wire );
        }
        doc["risks"] = risks;
    }
    return doc;
}

bool scientificPlanFromJson( const Json::Value &doc, ScientificPlan &out, std::string &error )
{
    out = ScientificPlan{};
    if ( !json_util::checkEnvelope( doc, kPlanEnvelopeKind, error ) )
        return false;
    if ( !json_util::readBoundedString( doc, "plan_id", kIdMax, out.planId, error ) )
        return false;
    if ( !json_util::readBoundedString( doc, "goal_id", kIdMax, out.goalId, error ) )
        return false;
    if ( !json_util::readBoundedString( doc, "goal_kind", kIdMax, out.goalKind, error ) )
        return false;
    if ( !isKnownGoalKind( out.goalKind ) )
    {
        error = json_util::error( "invalid_field", "unknown goal kind \"" + out.goalKind + "\"" );
        return false;
    }
    if ( !json_util::readBoundedString( doc, "rules_revision", kIdMax, out.rulesRevision, error ) )
        return false;

    if ( !doc.isMember( "mode" ) || !doc["mode"].isObject() )
    {
        error = json_util::error( "invalid_field", "mode must be an object" );
        return false;
    }
    if ( !json_util::readBoundedString( doc["mode"], "kind", kIdMax, out.modeKind, error ) )
        return false;
    if ( !isKnownModeKind( out.modeKind ) )
    {
        error = json_util::error( "invalid_field", "unknown mode kind \"" + out.modeKind + "\"" );
        return false;
    }
    if ( !json_util::readBoundedString( doc["mode"], "autonomy", kIdMax, out.autonomy, error ) )
        return false;
    if ( !isKnownAutonomy( out.autonomy ) )
    {
        error = json_util::error( "invalid_field", "unknown autonomy \"" + out.autonomy + "\"" );
        return false;
    }

    if ( !json_util::readBoundedString( doc, "verdict", kIdMax, out.verdict, error ) )
        return false;
    if ( !isKnownVerdict( out.verdict ) )
    {
        error = json_util::error( "invalid_field", "unknown verdict \"" + out.verdict + "\"" );
        return false;
    }
    if ( doc.isMember( "verdict_reasons" ) )
    {
        if ( !doc["verdict_reasons"].isArray()
             || !boundedStringList( doc["verdict_reasons"], kTextMax, out.verdictReasons, error,
                                    "verdict_reasons" ) )
        {
            if ( error.empty() )
                error = json_util::error( "invalid_field", "verdict_reasons must be an array" );
            return false;
        }
    }

    if ( !doc.isMember( "steps" ) || !doc["steps"].isArray() )
    {
        error = json_util::error( "invalid_field", "steps must be an array" );
        return false;
    }
    if ( static_cast<int>( doc["steps"].size() ) > PlanLimits::kMaxSteps )
    {
        error = json_util::error( "out_of_bounds",
                       "steps over " + std::to_string( PlanLimits::kMaxSteps ) );
        return false;
    }
    std::set<std::string> seenStepIds;
    for ( const auto &stepJson : doc["steps"] )
    {
        PlannerStep step;
        if ( !readStep( stepJson, step, error ) )
            return false;
        if ( !seenStepIds.insert( step.stepId ).second )
        {
            error = json_util::error( "invalid_field", "duplicate step id \"" + step.stepId + "\"" );
            return false;
        }
        // Wiring must reference EARLIER steps only: acyclic by construction.
        for ( const auto &input : step.inputs )
        {
            if ( !input.fromStepId.empty() && !seenStepIds.count( input.fromStepId ) )
            {
                error = json_util::error( "invalid_field",
                               "step \"" + step.stepId + "\" input references unknown or later step \""
                                   + input.fromStepId + "\"" );
                return false;
            }
        }
        out.steps.push_back( step );
    }

    if ( doc.isMember( "alternatives" ) )
    {
        if ( !doc["alternatives"].isArray() )
        {
            error = json_util::error( "invalid_field", "alternatives must be an array" );
            return false;
        }
        if ( static_cast<int>( doc["alternatives"].size() ) >= PlanLimits::kMaxCandidates )
        {
            error = json_util::error( "out_of_bounds",
                           "alternatives over " + std::to_string( PlanLimits::kMaxCandidates - 1 ) );
            return false;
        }
        for ( const auto &entry : doc["alternatives"] )
        {
            if ( !entry.isObject() )
            {
                error = json_util::error( "invalid_field", "alternatives[] must be objects" );
                return false;
            }
            PlanAlternative alternative;
            if ( !json_util::readBoundedString( entry, "alternative_id", kIdMax,
                                                alternative.alternativeId, error ) )
                return false;
            if ( !json_util::readBoundedString( entry, "summary", kTextMax, alternative.summary,
                                                error ) )
                return false;
            if ( !json_util::readOptionalBoundedString( entry, "why_chosen", kTextMax,
                                                        alternative.whyChosen, error ) )
                return false;
            if ( !json_util::readOptionalBoundedString( entry, "why_not", kTextMax,
                                                        alternative.whyNot, error ) )
                return false;
            if ( !entry.isMember( "candidate_index" ) || !entry["candidate_index"].isInt() )
            {
                error = json_util::error( "invalid_field", "alternatives[].candidate_index must be an int" );
                return false;
            }
            alternative.candidateIndex = entry["candidate_index"].asInt();
            if ( alternative.candidateIndex < 0
                 || alternative.candidateIndex >= PlanLimits::kMaxCandidates )
            {
                error = json_util::error(
                    "out_of_bounds", "alternatives[].candidate_index must be within [0,"
                                         + std::to_string( PlanLimits::kMaxCandidates ) + ")" );
                return false;
            }
            out.alternatives.push_back( alternative );
        }
    }

    if ( doc.isMember( "open_questions" ) )
    {
        if ( !doc["open_questions"].isArray() )
        {
            error = json_util::error( "invalid_field", "open_questions must be an array" );
            return false;
        }
        for ( const auto &entry : doc["open_questions"] )
        {
            if ( !entry.isObject() )
            {
                error = json_util::error( "invalid_field", "open_questions[] must be objects" );
                return false;
            }
            PlanOpenQuestion question;
            if ( !json_util::readBoundedString( entry, "question_id", kIdMax, question.questionId,
                                                error ) )
                return false;
            if ( !json_util::readBoundedString( entry, "kind", kIdMax, question.kind, error ) )
                return false;
            if ( !isKnownQuestionKind( question.kind ) )
            {
                error = json_util::error( "invalid_field", "unknown question kind \"" + question.kind + "\"" );
                return false;
            }
            if ( !entry.isMember( "blocking" ) || !entry["blocking"].isBool() )
            {
                error = json_util::error( "invalid_field", "open_questions[].blocking must be a bool" );
                return false;
            }
            question.blocking = entry["blocking"].asBool();
            if ( !json_util::readBoundedString( entry, "detail", kTextMax, question.detail,
                                                error ) )
                return false;
            if ( !json_util::readOptionalBoundedString( entry, "suggestion", kTextMax,
                                                        question.suggestion, error ) )
                return false;
            out.openQuestions.push_back( question );
        }
        std::set<std::string> questionIds;
        for ( const auto &question : out.openQuestions )
        {
            if ( !questionIds.insert( question.questionId ).second )
            {
                error = json_util::error( "invalid_field",
                                          "duplicate question id \"" + question.questionId
                                              + "\"" );
                return false;
            }
        }
    }

    if ( !doc.isMember( "cost" ) || !doc["cost"].isObject() )
    {
        error = json_util::error( "invalid_field", "cost must be an object" );
        return false;
    }
    if ( !json_util::readBoundedString( doc["cost"], "aggregate_cost_class", kIdMax,
                                        out.cost.aggregateCostClass, error ) )
        return false;
    if ( !isKnownCostClass( out.cost.aggregateCostClass ) )
    {
        error = json_util::error( "invalid_field",
                       "unknown aggregate cost class \"" + out.cost.aggregateCostClass + "\"" );
        return false;
    }
    if ( doc["cost"].isMember( "total_estimated_ram_mb" ) )
    {
        if ( !doc["cost"]["total_estimated_ram_mb"].isInt64()
             || doc["cost"]["total_estimated_ram_mb"].asInt64() < 0 )
        {
            error = json_util::error( "invalid_field", "total_estimated_ram_mb must be non-negative" );
            return false;
        }
        out.cost.totalEstimatedRamMb = doc["cost"]["total_estimated_ram_mb"].asInt64();
    }

    if ( doc.isMember( "risks" ) )
    {
        if ( !doc["risks"].isArray() )
        {
            error = json_util::error( "invalid_field", "risks must be an array" );
            return false;
        }
        for ( const auto &entry : doc["risks"] )
        {
            if ( !entry.isObject() )
            {
                error = json_util::error( "invalid_field", "risks[] must be objects" );
                return false;
            }
            PlanRisk risk;
            if ( !json_util::readBoundedString( entry, "kind", kIdMax, risk.kind, error ) )
                return false;
            if ( !isKnownRiskKind( risk.kind ) )
            {
                error = json_util::error( "invalid_field", "unknown risk kind \"" + risk.kind + "\"" );
                return false;
            }
            if ( !json_util::readBoundedString( entry, "detail", kTextMax, risk.detail, error ) )
                return false;
            if ( !json_util::readOptionalBoundedString( entry, "step_id", kIdMax, risk.stepId,
                                                        error ) )
                return false;
            out.risks.push_back( risk );
        }
    }
    return true;
}

std::string scientificPlanFingerprint( const ScientificPlan &plan )
{
    Json::Value doc = scientificPlanToJson( plan );
    doc.removeMember( "plan_id" ); // identity excludes the mutable plan id
    return sicnu::planner::fingerprint16( json_util::canonicalCompact( doc ) );
}

std::vector<std::string> validateScientificPlan( const ScientificPlan &plan )
{
    std::vector<std::string> problems;
    if ( plan.planId.empty() || plan.planId.size() > kIdMax )
        problems.push_back( "out_of_bounds: plan_id empty or over 64 chars" );
    if ( !isKnownGoalKind( plan.goalKind ) )
        problems.push_back( "invalid_field: unknown goal kind" );
    if ( !isKnownVerdict( plan.verdict ) )
        problems.push_back( "invalid_field: unknown verdict" );
    if ( !isKnownModeKind( plan.modeKind ) || !isKnownAutonomy( plan.autonomy ) )
        problems.push_back( "invalid_field: unknown mode policy" );
    if ( int( plan.steps.size() ) > PlanLimits::kMaxSteps )
        problems.push_back( "out_of_bounds: too many steps" );

    std::set<std::string> stepIds;
    for ( const auto &step : plan.steps )
    {
        if ( step.stepId.empty() || step.stepId.size() > kIdMax )
            problems.push_back( "out_of_bounds: step id empty or over 64 chars: " + step.stepId );
        if ( !stepIds.insert( step.stepId ).second )
            problems.push_back( "invalid_field: duplicate step id " + step.stepId );
        if ( !isKnownStepRole( step.role ) )
            problems.push_back( "invalid_field: unknown role " + step.role );
        if ( !isKnownFamilySlot( step.family ) )
            problems.push_back( "invalid_field: unknown family " + step.family );
        if ( !isKnownCostClass( step.costClass ) )
            problems.push_back( "invalid_field: unknown cost class " + step.costClass );
        for ( const auto &input : step.inputs )
        {
            if ( input.fromStepId.empty() == input.assetRef.empty() )
                problems.push_back( "invalid_field: step input must be exactly one form: "
                                    + step.stepId );
            if ( !input.fromStepId.empty() && !stepIds.count( input.fromStepId ) )
                problems.push_back( "invalid_field: dangling step input in " + step.stepId );
        }
        for ( const auto &transition : step.expectedTransitions )
        {
            if ( !isKnownContractsNumericDomain( transition.fromDomain )
                 || !isKnownContractsNumericDomain( transition.toDomain ) )
                problems.push_back( "invalid_field: transition domain off contracts vocabulary in "
                                    + step.stepId );
        }
    }
    for ( const auto &question : plan.openQuestions )
    {
        if ( !isKnownQuestionKind( question.kind ) )
            problems.push_back( "invalid_field: unknown question kind " + question.kind );
        if ( question.questionId.empty() )
            problems.push_back( "out_of_bounds: open question without identity" );
    }
    for ( const auto &alternative : plan.alternatives )
    {
        if ( alternative.candidateIndex < 0
             || alternative.candidateIndex >= PlanLimits::kMaxCandidates )
            problems.push_back( "out_of_bounds: alternative candidate_index out of range" );
    }
    for ( const auto &risk : plan.risks )
    {
        if ( !isKnownRiskKind( risk.kind ) )
            problems.push_back( "invalid_field: unknown risk kind " + risk.kind );
    }
    const bool hasBlocking =
        std::any_of( plan.openQuestions.begin(), plan.openQuestions.end(),
                     []( const PlanOpenQuestion &q ) { return q.blocking; } );
    if ( plan.verdict == "feasible" && hasBlocking )
        problems.push_back( "invalid_field: feasible verdict with blocking questions" );
    return problems;
}

std::string scientificPlanSequenceSummary( const ScientificPlan &plan )
{
    std::string summary;
    for ( const auto &step : plan.steps )
    {
        if ( !summary.empty() )
            summary += "->";
        summary += step.role;
        if ( !step.operatorId.empty() )
        {
            summary += ":";
            summary += step.operatorId;
        }
    }
    return summary;
}

} // namespace sicnu::planner

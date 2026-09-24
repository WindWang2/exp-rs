// src/planner/plan_teaching.cpp
#include "planner/plan_teaching.h"

#include "planner/planner_rules.h"

namespace sicnu::planner {

namespace {

bool maskingPolicyActive( const ModePolicy &mode )
{
    return mode.kind == "teaching" && ( mode.autonomy == "guided" || mode.autonomy == "minimal" );
}

} // namespace

TeachingViews teachingViews( const ScientificPlan &plan, const ModePolicy &mode )
{
    TeachingViews views;
    Json::Value base = scientificPlanToJson( plan );
    const bool mask = maskingPolicyActive( mode );

    // ---- hidden-answer view ---------------------------------------------
    Json::Value hidden = base;
    if ( mask )
    {
        Json::Value maskedSteps( Json::arrayValue );
        for ( const auto &step : base["steps"] )
        {
            Json::Value wire = step;
            if ( step.isMember( "student_decision" ) && step["student_decision"].asBool() )
            {
                Json::Value placeholder( Json::objectValue );
                placeholder["masked"] = true;
                placeholder["reason"] = "student_decision";
                wire["params"] = placeholder;
            }
            maskedSteps.append( wire );
        }
        hidden["steps"] = maskedSteps;
    }
    hidden["masking_applied"] = mask;
    hidden["masking_policy"] = mask ? "teaching:" + mode.autonomy : "none";
    views.hiddenAnswer = hidden;

    // ---- explanation view -------------------------------------------------
    Json::Value explained = base;
    Json::Value explanations( Json::arrayValue );
    for ( const auto &step : plan.steps )
    {
        Json::Value entry( Json::objectValue );
        entry["step_id"] = step.stepId;
        entry["rationale"] = stageRationale( plan.goalKind, step.role, step.family );
        Json::Value whys( Json::arrayValue );
        for ( const auto &transition : step.expectedTransitions )
        {
            whys.append( "asset " + transition.assetRef + " moves " + transition.fromDomain
                         + "→" + transition.toDomain + " because " + step.family + " step "
                         + ( step.operatorId.empty() ? std::string( "(to be decided)" )
                                                     : step.operatorId )
                         + " produces that scale" );
        }
        if ( whys.size() > 0 )
            entry["transition_whys"] = whys;
        entry["student_decision"] = step.studentDecision;
        explanations.append( entry );
    }
    explained["explanations"] = explanations;

    Json::Value thinking( Json::arrayValue );
    for ( const auto &question : plan.openQuestions )
    {
        if ( question.kind == "insufficient_data" )
            continue; // data gaps are not discussion prompts
        Json::Value entry( Json::objectValue );
        entry["question_id"] = question.questionId;
        entry["prompt"] = question.detail;
        entry["kind"] = question.kind;
        thinking.append( entry );
    }
    explained["thinking_questions"] = thinking;
    explained["masking_applied"] = mask;
    views.explanation = explained;
    return views;
}

} // namespace sicnu::planner

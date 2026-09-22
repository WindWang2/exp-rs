// src/science_context/planner_projection.h
#pragma once

#include "science_context/bundle.h"
#include <json/json.h>
#include <string>

namespace sicnu::science_context {

struct PlanningContext
{
    std::string goal;
    std::string intent;
    std::string recipeId;
    Json::Value recipeBindings{Json::objectValue};
    Json::Value inputFacts{Json::objectValue};
    Json::Value modelContracts{Json::objectValue};
    bool applyRepairs = true;
    bool executionBlocked = false;
    std::string blockReason;
    Json::Value missingFacts{Json::arrayValue};
    Json::Value limitations{Json::arrayValue};
    Json::Value openQuestions{Json::arrayValue};
    Json::Value alternatives{Json::arrayValue};
};

PlanningContext projectPlanningContext( const ScientificContextBundle &bundle );
Json::Value planningContextToCompileRequest( const PlanningContext &ctx );
void applyPlannerProjection( ScientificContextBundle &bundle );

} // namespace sicnu::science_context

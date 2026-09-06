// src/agent/harness/recipe_tools.h
#pragma once

#include "spatial_tools/spatial_tool.h"

namespace sicnu::agent::harness {

/// Registers the recipe discovery tools (mission Phase 16):
///   harness:search_recipes    bounded recipe summaries
///   harness:describe_recipe   full recipe document (steps + slots)
///   harness:instantiate_recipe recipe + bindings -> AgentPlan v2 document
/// Idempotent.
void registerRecipeTools();

} // namespace sicnu::agent::harness

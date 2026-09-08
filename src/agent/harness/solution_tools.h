// src/agent/harness/solution_tools.h
#pragma once

#include "../spatial_tools/spatial_tool.h"

namespace sicnu::agent::harness {

/// Registers the Platform 5.0 solution:* agent tools (idempotent):
///   solution:search       faceted search over solution templates
///   solution:describe     full solution document + resolved references
///   solution:validate     contract/reference validation with wired resolvers
///   solution:instantiate  bindings -> {plan, mapspec, styles, artifacts}
/// The instantiation pipeline composes the authoritative surfaces
/// (RecipeCatalog -> AgentPlan v2, TemplateRegistry -> MapSpec draft,
/// StyleRegistry -> token-resolved styles); it never executes operators.
void registerSolutionTools();

} // namespace sicnu::agent::harness

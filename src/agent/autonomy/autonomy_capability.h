// src/agent/autonomy/autonomy_capability.h
#pragma once

//
// RS14-12: the closed capability taxonomy.
//
// Every assistance or action an autonomy policy governs is expressed as one
// capability. Each capability has a MINIMUM autonomy level; the ladder level
// of a session is compared against it by the decision engine. The taxonomy is
// closed and versioned with the policy schema: a new capability requires a
// schema bump, so old policy documents can never silently authorize a
// capability they never knew about.
//
// The capability is the semantic projection of an intent (assistance) or of a
// tool/action's risk class (execution); classification inputs live in
// autonomy_classification.h so this header stays a pure value vocabulary.

#include <string>

#include "agent/autonomy/autonomy_level.h"

namespace sicnu::agent::autonomy {

/// Capability wire strings. Never rename.
namespace assistance_capabilities
{
inline constexpr const char *kReadOnlyQuery = "read_only_query";
inline constexpr const char *kConceptHint = "concept_hint";
inline constexpr const char *kErrorLocalization = "error_localization";
inline constexpr const char *kNextStepRecommendation = "next_step_recommendation";
inline constexpr const char *kPlanGeneration = "plan_generation";
inline constexpr const char *kAutonomousExecution = "autonomous_execution";
} // namespace assistance_capabilities

/// All capabilities, in ladder order (deterministic for projections).
const std::string *autonomyCapabilityVocabulary( int &count );

/// True when `capability` is part of the closed taxonomy.
bool isKnownAssistanceCapability( const std::string &capability );

/// Minimum ladder level that unlocks `capability`. Undefined for unknown
/// capabilities — callers check isKnownAssistanceCapability first; the
/// decision engine treats unknown as deny.
AutonomyLevel minimumLevelForCapability( const std::string &capability );

/// The highest capability whose minimum level is <= `level` (the downgrade
/// target when a request exceeds the effective level). Always at least
/// read_only_query — a session can always be told what it may look at.
std::string highestCapabilityWithinLevel( AutonomyLevel level );

} // namespace sicnu::agent::autonomy

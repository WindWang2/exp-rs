// src/agent/harness/capability_graph.h
#pragma once

//
// Harness 7.0 intent → capability graph (mission Area B).
//
// Deterministic bridge from a free-text goal to the closed scientific intent
// vocabulary and from there to concrete, feasibility-checked capability
// candidates. Nothing here guesses:
//   - clear evidence               → resolved intent + ranked candidates,
//   - competing evidence (a tie)   → typed INTENT_AMBIGUOUS with the tied
//                                    candidates and their matched evidence,
//   - no evidence at all           → typed INTENT_AMBIGUOUS ("no_evidence")
//                                    so the agent inspects the dataset
//                                    instead of inventing a plan.
// Feasibility is evaluated against DatasetUnderstanding *facts* (modality,
// band roles, radiometric state, temporal coverage) using the capability
// knowledge layer (Area A) — the same facts the scientific preflight reads.
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::agent::harness {

/// Result of classifying a goal text against the closed intent vocabulary.
struct IntentResolution {
  std::string intent;                     ///< resolved intent ("" unless resolved)
  std::string status;                     ///< "resolved" | "ambiguous" | "unresolved"
  Json::Value candidates{Json::arrayValue}; ///< [{intent, score, matched:[terms]}]
  Json::Value matchedTerms{Json::arrayValue}; ///< evidence for the resolution

  /// Typed INTENT_AMBIGUOUS error carrying the candidate list; valid when
  /// status != "resolved".
  Json::Value ambiguityError() const;
};

/// Deterministic classification of free-text goal text. Same input, same
/// output — no model calls, no randomness.
IntentResolution resolveGoalIntent( const std::string &goalText );

/// Feasibility of one merged capability entry against a DatasetUnderstanding
/// document (or null when no facts are known). Wire shape:
/// {capability_id, feasible, score, why[], why_not[]} where why_not entries
/// are {code, message} with codes from the stable error taxonomy.
Json::Value evaluateFeasibility( const Json::Value &capabilityEntry,
                                 const Json::Value &understanding );

/// Ranked, feasibility-filtered capability candidates for `intent` over an
/// understanding document (may be null): {intent, candidates:[...],
/// total}. Candidates order: feasible first, then score descending, then id.
Json::Value capabilityCandidates( const std::string &intent,
                                  const Json::Value &understanding );

/// Registers the harness:resolve_intent tool on the SpatialToolRegistry
/// (called once from the built-in tool registration).
void registerCapabilityGraphTools();

} // namespace sicnu::agent::harness

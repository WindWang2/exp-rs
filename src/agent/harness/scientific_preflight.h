// src/agent/harness/scientific_preflight.h
#pragma once

//
// Harness 4.0 deterministic scientific preflight (mission Phase 5).
//
// Rule packs keyed by scientific intent. Every rule reads *resolved facts*
// (raster inspection documents, never prose): band roles, wavelengths,
// radiometric state, CRS, grid, modality, polarizations, acquisition times.
//
// Verdict vocabulary mirrors the 3.0 PreflightResult contract:
//   "ok" | "fixable" | "blocked"  — blocked plans are refused by the plan
// runner; the LLM cannot override a blocked verdict, and a blocked rule pack
// emits typed HarnessError codes (Phase 12), never prose guesses.
//

#include <json/json.h>
#include <string>
#include <vector>

#include "harness_error.h"

namespace sicnu::agent::harness {

/// One resolved preflight input: a named slot plus the facts gathered for it.
struct PreflightInput {
  std::string name;      ///< Slot name, e.g. "primary", "secondary", "training".
  std::string reference; ///< What the caller asked to resolve.
  Json::Value understanding; ///< DatasetUnderstanding document (may be null).
  HarnessError resolutionError; ///< Set when the reference did not resolve.
  bool resolved() const { return understanding.isObject(); }

  /// Harness 7.0 optional facts. All default to "unknown" and every rule pack
  /// must degrade to warnings — facts narrow the checks, they never fake them.
  /// {scene_count, dates[], max_gap_days?} from a temporal collection.
  Json::Value temporalFacts{Json::Value()};
  /// Projected model manifest (ModelCatalog) for inference intents.
  Json::Value modelManifest{Json::Value()};
  /// Explicit supervised/unsupervised classification switch (refs entry
  /// "supervised"); default true keeps the 4.0 training-required behavior.
  bool supervised = true;
};

struct PreflightOutcome {
  std::string verdict;                 ///< "ok" | "fixable" | "blocked"
  Json::Value issues{Json::arrayValue};///< PreflightResult issue objects
  Json::Value checks{Json::arrayValue};///< {check, passed, severity, code, details}
  std::vector<HarnessError> errors;    ///< typed errors backing blocker issues

  Json::Value toJson( const std::string &subject ) const;
};

/// Runs the rule pack for `intent` over the resolved inputs. Unknown intents
/// run only the shared rules (every input resolved, rasters inspectable).
PreflightOutcome runScientificPreflight( const std::string &intent,
                                         const std::vector<PreflightInput> &inputs );

/// Convenience: resolves `refs` (name → reference) through resolveDatasetRef
/// and gathers DatasetUnderstanding facts (raster inspect; vectors tolerated)
/// before running the rule pack. Non-resolving slots become blocker issues
/// (DATASET_NOT_FOUND) instead of exceptions. Refs entries may carry
/// Harness 7.0 extras: "model" (model catalog id), "supervised" (bool),
/// "temporal_facts" (object).
PreflightOutcome preflightIntent( const std::string &intent,
                                  const Json::Value &refs );

/// True when the rule pack for `intent` needs >=2 comparable rasters
/// (change, sar_change) — surfaced for plan drafting guidance.
bool intentRequiresPair( const std::string &intent );

/// Machine-readable mirror of the rule-pack table (Harness 7.0 Area A drift
/// anchor): the pack kind, band-role minimums, pair requirement, and modality
/// expectation enforced for `intent`. Null for unknown intents. The drift
/// test pins the capability-knowledge layer to this table so the JSON layer
/// and the C++ rules cannot silently diverge.
Json::Value intentRequirements( const std::string &intent );

} // namespace sicnu::agent::harness

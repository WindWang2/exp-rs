// src/agent/harness/intent_vocabulary.h
#pragma once

//
// Single source of truth for the closed scientific intent vocabulary.
//
// Every intent id is declared exactly once here; the membership checks and
// dispatch tables (agent_plan.cpp, scientific_preflight.cpp,
// capability_knowledge.cpp, capability_graph.cpp) key off these constants
// instead of repeating raw string literals that can silently drift apart.
//
// Declaration order is historical and pinned by tests/test_capability_drift.cpp:
// the Harness 4.0 vocabulary + Platform 5.0 recipe families, then the
// Harness 9.0 (M2) zonal addition. Keep the order stable — mirrors and
// error strings reference it.
//
// Adding an intent: add the constant here, list it in kIntentVocabulary, and
// extend the tables that give it behavior (preflight spec, triggers,
// capability knowledge) — never a new copy of the strings.
//

namespace sicnu::agent::harness {

inline constexpr const char *kIntentNdvi = "ndvi";
inline constexpr const char *kIntentChange = "change";
inline constexpr const char *kIntentSarChange = "sar_change";
inline constexpr const char *kIntentClassify = "classify";
inline constexpr const char *kIntentPhenology = "phenology";
inline constexpr const char *kIntentEvi = "evi";
inline constexpr const char *kIntentSavi = "savi";
inline constexpr const char *kIntentNdre = "ndre";
inline constexpr const char *kIntentNdwi = "ndwi";
inline constexpr const char *kIntentMndwi = "mndwi";
inline constexpr const char *kIntentNdsi = "ndsi";
inline constexpr const char *kIntentNbr = "nbr";
inline constexpr const char *kIntentDnbr = "dnbr";
inline constexpr const char *kIntentNdbi = "ndbi";
inline constexpr const char *kIntentBsi = "bsi";
inline constexpr const char *kIntentWater = "water";
inline constexpr const char *kIntentFlood = "flood";
inline constexpr const char *kIntentSarWater = "sar_water";
inline constexpr const char *kIntentSarFlood = "sar_flood";
inline constexpr const char *kIntentSar = "sar";
inline constexpr const char *kIntentShip = "ship";
inline constexpr const char *kIntentTemporal = "temporal";
inline constexpr const char *kIntentTerrain = "terrain";
inline constexpr const char *kIntentAccuracy = "accuracy";
inline constexpr const char *kIntentQa = "qa";
inline constexpr const char *kIntentPreprocess = "preprocess";
inline constexpr const char *kIntentInference = "inference";
// Harness 9.0 (M2): zonal raster statistics over vector zones.
inline constexpr const char *kIntentZonal = "zonal";

/// The closed intent vocabulary, in pinned declaration order. "" (empty)
/// means a custom plan with no intent-specific preflight and stays outside
/// this list (see isKnownIntent()).
inline constexpr const char *const kIntentVocabulary[] = {
  kIntentNdvi, kIntentChange, kIntentSarChange, kIntentClassify, kIntentPhenology,
  kIntentEvi, kIntentSavi, kIntentNdre, kIntentNdwi, kIntentMndwi, kIntentNdsi,
  kIntentNbr, kIntentDnbr, kIntentNdbi, kIntentBsi,
  kIntentWater, kIntentFlood, kIntentSarWater, kIntentSarFlood, kIntentSar, kIntentShip,
  kIntentTemporal, kIntentTerrain, kIntentAccuracy, kIntentQa, kIntentPreprocess,
  kIntentInference,
  kIntentZonal,
};

} // namespace sicnu::agent::harness

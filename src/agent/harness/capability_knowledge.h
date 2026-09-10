// src/agent/harness/capability_knowledge.h
#pragma once

//
// Harness 7.0 machine-decidable capability knowledge (mission Area A).
//
// Structured, typed knowledge entries keyed by operator id that describe what
// a capability *requires* and *produces* — applicability, input modality,
// band roles, CRS/grid demands, radiometric state, SAR requirements, temporal
// requirements, model compatibility, resource hints, side effects, output
// artifact contract, limitations, and the verification checks its outputs
// must pass. The facts live in data/agent/capabilities/*.json; this module
// only loads, validates (fail-closed, like RecipeCatalog), merges family
// defaults, and answers bounded queries. It never executes anything.
//
// Anti-drift contract: knowledge is a *derivation target*, not a second
// schema source. tests/test_capability_drift.cpp cross-checks entries against
// the live operator registry, the operator-declared x-rs-contract objects,
// the closed intent vocabulary, and the machine-readable preflight
// requirements (intentRequirements) so prose and code cannot diverge.
//
// Entry shape (all fields optional except id/family; unknown keys rejected):
// {
//   "id": "rs:ndvi",                       — operator id (registry key) or
//                                            "family:<name>" for a default
//   "family": "spectral_index",            — required routing/fallback key
//   "extends": "family:spectral_index",    — optional single parent
//   "intents": ["ndvi"],                   — closed intent vocabulary served
//   "variants": [ { "when": {"param": "index", "values": ["NDVI"]},
//                   "intents": ["ndvi"], "band_roles": {...}, ... } ],
//   "modality": ["optical"],               — input modality demands
//   "band_roles": {"nir": 1, "red": 1},    — role -> min count on primary input
//   "radiometric": {"acceptable": ["surface_reflectance","toa"], "warn": ["dn"]},
//   "crs": {"requires_projected": false},
//   "sar": {"polarizations": ["vv","vh"], "calibration": ["sigma0","gamma0"]},
//   "temporal": {"min_scenes": 0, "requires_acquisition_time": false,
//                "max_gap_days": 0},
//   "model_compatibility": {"families": ["segmentation"],
//                            "input_band_roles": {"rgb": 3}},
//   "resource": {"cost_class": "light", "large_raster_safe": true},
//   "side_effects": false,
//   "artifacts": {"output": {"kind": "raster", "dtype": "float32"}},
//   "verification": {"expected_kind": "raster",
//                     "checks": ["finite_fraction","nodata_fraction"]},
//   "applicability": {"resolution_range": [0.0, 100.0],
//                      "modalities": ["optical"]},
//   "limitations": ["..."]
// }
//
// Variants are the parameter-conditional override mechanism (e.g. one
// spectral_index operator serving ten indices with different band needs);
// they never introduce new top-level keys and resolve deterministically by
// first match in declaration order.
//

#include <json/json.h>
#include <string>
#include <vector>

#include "harness_error.h"

namespace sicnu::agent::harness {

class CapabilityKnowledge {
  public:
    static CapabilityKnowledge &instance();

    /// Directory override (tests); empty restores the default search path
    /// ($SICNU_CAPABILITIES_DIR, <cwd>/data/agent/capabilities,
    /// <app>/../data/agent/capabilities, SICNU_SOURCE_DIR/data/agent/capabilities
    /// — same policy as RecipeCatalog/ModelCatalog).
    void setDirectory( const std::string &directory );
    std::string directory() const;

    /// (Re)scans the directory. Returns the number of valid entries loaded.
    int reload();

    bool loaded() const { return mLoaded; }

    /// Problems recorded while loading (invalid entries were skipped).
    std::vector<std::string> loadProblems() const;

    /// The merged (extends + family default + variant) knowledge entry for an
    /// operator id, or null when unknown. `variantParams` (e.g. {"index":
    /// "NDVI"}) select the first matching variant; the variant's keys override
    /// the base entry's keys at the top level.
    Json::Value entryForOperator( const std::string &operatorId,
                                  const Json::Value &variantParams = Json::Value() ) const;

    /// The un-merged, un-varianted document as loaded (drift tests inspect
    /// variants directly). Null when unknown.
    Json::Value rawEntry( const std::string &operatorId ) const;

    /// Operator ids (in load order) whose entry (or one of its variants)
    /// declares `intent`. Empty when the intent serves no known capability.
    std::vector<std::string> operatorsForIntent( const std::string &intent ) const;

    /// The family-default document for `family`, or null.
    Json::Value familyDefault( const std::string &family ) const;

    /// All loaded entry ids (operator ids, family defaults excluded).
    std::vector<std::string> entryIds() const;

    /// Harness 8.0: entry ids whose raw entry declares `surface` (missing
    /// surface = "operator"). Surfaces: "operator" resolves against
    /// RSOperatorRegistry, "spatial_tool" against SpatialToolRegistry,
    /// "data_platform_tool" against dataPlatformToolDefs(); the drift test
    /// cross-checks each against its authoritative registry.
    std::vector<std::string> entryIdsForSurface( const std::string &surface ) const;

    /// Structural + vocabulary validation of one raw entry. Returns one
    /// problem per finding; empty means valid. Static so the drift test can
    /// validate documents without going through the directory scan.
    static std::vector<std::string> validateEntry( const Json::Value &entry );

    /// True when `entry` (raw or merged) demands `role` on the primary input.
    /// Counts: band_roles maps role -> minimum count (missing = 0).
    static int bandRoleMinimum( const Json::Value &entry, const std::string &role );

  private:
    CapabilityKnowledge() = default;
    std::string defaultDirectory() const;

    /// Resolves the extends/family chain (cycle- and depth-bounded) and
    /// applies variant overrides for the operator entry `raw`.
    Json::Value mergedEntry( const Json::Value &raw,
                             const Json::Value &variantParams ) const;

    std::string mDirectory;
    bool mLoaded = false;
    Json::Value mEntries{Json::objectValue};    ///< operator id -> raw entry
    Json::Value mFamilyDefaults{Json::objectValue}; ///< family -> raw entry
    std::vector<std::string> mOrder;            ///< entry ids in load order
    std::vector<std::string> mLoadProblems;
};

} // namespace sicnu::agent::harness

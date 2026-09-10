// src/agent/harness/recipe_catalog.h
#pragma once

//
// Harness 4.0 scientific recipes (mission Phase 16).
//
// Recipes are metadata under data/agent/recipes/*.json that *orchestrate
// existing operators* into AgentPlan v2 documents. They never carry kernels:
// the operator registry stays the single algorithm authority. The mini-language
// is three tokens and two step gates:
//   "$<slot>.path"        — bound slot's resolved path/reference
//   "$outputs.<name>"     — derived output path for a declared output
//   "$params.<key>"       — a caller binding under bindings.params
//   "when_slot": "name"   — step included only when that slot is bound
//   "when_slots": [names] — step included only when ALL named slots are
//                           bound (fusion branches)
//   "when_param": "key"   — step included only when bindings.params[key] is
//                           truthy; downstream steps then read their
//                           "params_when_skipped" template instead of
//                           "params"; degradation propagates along declared
//                           step "inputs" wiring only (per branch, #784)
//
// Harness 7.0 (mission Area G) adds two de-duplication mechanisms so the
// catalog does not grow one near-clone file per sensor/filter/classifier:
//   "aliases": ["<old recipe_id>", ...] — deleted near-clone ids resolve to
//                                         this canonical document.
//   "presets": { "<name>": { "step_params": { "<step_id>": {..overrides..} },
//                             "keep_outputs": ["<output name>", ...],
//                             "<flat param key>": value, ... } }
//     chosen via bindings.preset. Flat keys override ANY step param with the
//     same name (the flood-mapping shape); "step_params" overrides params of
//     one named step; "keep_outputs" filters declared outputs. Application is
//     deterministic and validated at load (preset step/output names must
//     exist).
//

#include <json/json.h>
#include <string>
#include <vector>

#include "harness_error.h"

namespace sicnu::agent::harness {

class RecipeCatalog {
  public:
    static RecipeCatalog &instance();

    /// Directory override (tests); empty restores the default search path
    /// ($SICNU_RECIPES_DIR, <cwd>/data/agent/recipes, <app>/../data/agent/recipes,
    /// SICNU_SOURCE_DIR/data/agent/recipes — same policy as ModelCatalog).
    void setDirectory( const std::string &directory );
    std::string directory() const;

    /// (Re)scans the directory. Returns the number of valid recipes loaded.
    int reload();

    /// Bounded summaries: [{recipe_id, title, intent, slots, step_count}]
    /// plus, when declared, capabilities and applicability modalities
    /// (Platform 6.0 decisionable-knowledge metadata).
    Json::Value listRecipes() const;

    /// Full recipe document; typed failure (empty Json) when unknown.
    /// Alias ids (Harness 7.0) resolve to their canonical document.
    Json::Value recipe( const std::string &recipeId ) const;

    /// Instantiates a recipe into an AgentPlan v2 document. `bindings`:
    /// {slots: {name: "<dataset ref>"}, params: {...}, output_dir: "...",
    ///  outputs: {name: "path"}, preset: "<name>"}. Deterministic: same
    /// inputs, same plan. Fails (empty Json + typed error) on unknown slots
    /// or unresolvable refs (slot refs are resolved through resolveDatasetRef
    /// — no guessing). Gate semantics (Platform 6.0, #784): when_slot/
    /// when_slots/when_param gate a step; degradation runs along the declared
    /// step "inputs" wiring only — an unrelated closed gate never flips a
    /// parallel branch.
    Json::Value instantiateRecipe( const std::string &recipeId, const Json::Value &bindings,
                                   HarnessError &error ) const;

    /// Structural validation of the Platform 6.0 decisionable-knowledge
    /// metadata (capabilities, applicability, presets, limitations,
    /// expected_artifacts, quality_gates) plus the Harness 7.0 alias/preset
    /// internals. All fields optional; present fields are shape- and
    /// budget-checked. Empty returned vector = valid.
    static std::vector<std::string> validateRecipeMetadata( const Json::Value &recipe );

    /// Problems recorded while loading (invalid metadata skipped a document).
    std::vector<std::string> loadProblems() const;

    bool loaded() const { return mLoaded; }

  private:
    RecipeCatalog() = default;
    std::string defaultDirectory() const;

    std::string mDirectory;
    bool mLoaded = false;
    Json::Value mRecipes{Json::Value( Json::objectValue )}; // recipe_id -> document
    Json::Value mAliases{Json::Value( Json::objectValue )}; // alias -> canonical id
    std::vector<std::string> mLoadProblems;
};

} // namespace sicnu::agent::harness


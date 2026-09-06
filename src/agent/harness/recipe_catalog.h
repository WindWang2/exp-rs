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
//   "when_param": "key"   — step included only when bindings.params[key] is
//                           truthy; downstream steps then read their
//                           "params_when_skipped" template instead of "params"
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

    /// Bounded summaries: [{recipe_id, title, intent, slots, step_count}].
    Json::Value listRecipes() const;

    /// Full recipe document; typed failure (empty Json) when unknown.
    Json::Value recipe( const std::string &recipeId ) const;

    /// Instantiates a recipe into an AgentPlan v2 document. `bindings`:
    /// {slots: {name: "<dataset ref>"}, params: {...}, output_dir: "...",
    ///  outputs: {name: "path"}}. Deterministic: same inputs, same plan.
    /// Fails (empty Json + typed error) on unknown slots or unresolvable refs
    /// (slot refs are resolved through resolveDatasetRef — no guessing).
    Json::Value instantiateRecipe( const std::string &recipeId, const Json::Value &bindings,
                                   HarnessError &error ) const;

    bool loaded() const { return mLoaded; }

  private:
    RecipeCatalog() = default;
    std::string defaultDirectory() const;

    std::string mDirectory;
    bool mLoaded = false;
    Json::Value mRecipes{Json::objectValue}; // recipe_id -> document
};

} // namespace sicnu::agent::harness

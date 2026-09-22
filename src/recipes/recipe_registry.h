// src/recipes/recipe_registry.h
#pragma once

//
// RS14-20 ScientificRecipeRegistry — directory-backed store for compiled /
// authored scientific recipes under data/agent/scientific_recipes/*.json.
//
// Same fail-closed discipline as the harness catalogs: invalid documents are
// skipped and reported via loadProblems(); duplicate recipe_ids are a typed
// load problem, never a silent overwrite. This is a *new artifact kind* —
// it does not replace or shadow harness::RecipeCatalog (data/agent/recipes).
//
// Plain class (not a singleton): embedders/tests own their instance.
//

#include "recipes/recipe_diagnostics.h"

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace sicnu::recipes {

class ScientificRecipeRegistry
{
  public:
    /// Directory override; empty restores the default search path
    /// ($SICNU_SCIENTIFIC_RECIPES_DIR → <cwd>/data/agent/scientific_recipes →
    /// SICNU_SOURCE_DIR/data/agent/scientific_recipes).
    void setDirectory( const std::string &directory );
    std::string directory() const;

    /// (Re)scan. Returns the number of valid recipes loaded.
    int reload();
    bool loaded() const { return mLoaded; }

    /// "ok" | "unavailable" (missing dir or zero valid docs) | "degraded"
    /// (some docs failed validation).
    std::string status() const;
    const std::vector<std::string> &loadProblems() const { return mLoadProblems; }

    /// Sorted recipe ids.
    std::vector<std::string> recipeIds() const;

    /// Full recipe document, or null Json when unknown.
    Json::Value recipe( const std::string &recipeId ) const;

    /// Bounded summaries: [{recipe_id, title, intent, modality, stage_count,
    /// coverage}] sorted by id.
    Json::Value listRecipes( int page = 0, int pageSize = 64 ) const;

    /// Hard caps (lookup + list paging): keeps MCP/agent surfaces bounded.
    static constexpr int kMaxPageSize = 64;

  private:
    std::string defaultDirectory() const;

    std::string mDirectory;
    bool mLoaded = false;
    std::map<std::string, Json::Value> mRecipes;
    std::vector<std::string> mLoadProblems;
};

} // namespace sicnu::recipes

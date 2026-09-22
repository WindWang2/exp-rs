// src/recipes/recipe_lookup.h
#pragma once

//
// RS14-20 semantic lookup adapter.
//
// Read-only scored search over a ScientificRecipeRegistry: goal text /
// intent / modality / required-operator facets in, deterministic ranked hits
// out. This is the machine-readable seam a future agent tool (or the RS14-09
// planner) consumes — it never mutates the registry and never executes
// anything.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::recipes {

class ScientificRecipeRegistry;

/// Search criteria. Empty fields are ignored (match-all for that facet).
struct RecipeQuery
{
  std::string intent;                    ///< exact goal_pattern.intent match (+4)
  std::string text;                      ///< free text, tokenized (+1/keyword hit)
  std::string modality;                  ///< goal_pattern.modality match (+2)
  std::vector<std::string> operators;    ///< stage operator ids, +1 each covered
  int limit = 10;                        ///< hard-capped to registry page size
};

/// One scored hit: summary fields a chooser needs without loading the doc.
struct RecipeHit
{
  std::string recipeId;
  double score = 0.0;
  Json::Value summary{Json::objectValue}; ///< {recipe_id,title,intent,modality,stage_count,coverage,matched[]}
};

/// Deterministic scored search. Sort: score desc, recipe_id asc (stable).
/// Zero hits → empty vector (not an error).
std::vector<RecipeHit> searchRecipes( const ScientificRecipeRegistry &registry,
                                      const RecipeQuery &query );

/// Wire shape for tool surfaces: {hits:[summary…], total, query_echo}.
Json::Value searchRecipesJson( const ScientificRecipeRegistry &registry,
                               const RecipeQuery &query );

} // namespace sicnu::recipes

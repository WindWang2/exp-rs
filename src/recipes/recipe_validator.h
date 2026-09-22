// src/recipes/recipe_validator.h
#pragma once

//
// RS14-20 recipe validator/linter.
//
// Structural + semantic checks over a ScientificRecipe document. The C++
// validator is authoritative (same discipline as lab_spec_loader): closed
// key sets, closed vocabularies, typed diagnostics. The JSON Schema file
// (data/schemas/scientific_recipe.schema.json) mirrors it for external tools.
//

#include "recipes/recipe_diagnostics.h"

#include <json/json.h>

namespace sicnu::recipes {

/// Validate one recipe document. Empty result = clean. Errors use the
/// diag_codes vocabulary; `recipe_id` appears in messages for context.
RecipeDiagnostics validateRecipe( const Json::Value &recipe );

/// Convenience: validate + classify. Returns true when no Error severity.
bool recipeIsValid( const Json::Value &recipe, RecipeDiagnostics *diags = nullptr );

} // namespace sicnu::recipes

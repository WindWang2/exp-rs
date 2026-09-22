// src/science_context/recipe_router.h
#pragma once

#include "science_context/bundle.h"
#include <json/json.h>
#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::science_context {

struct RecipeDocument
{
    std::string recipeId;
    std::string title;
    std::string intent;
    std::string modality;
    std::vector<std::string> keywords;
    int stageCount = 0;
    bool hasHumanOnly = false;
    bool hasVerifierHooks = false;
    Json::Value requiredAssetHints{Json::arrayValue};
};

struct RecipeQuery
{
    std::string intent;
    std::string modality;
    std::string text;
    Json::Value observedState{Json::objectValue};
    int limit = 5;
};

struct RecipeRouterResult
{
    std::vector<RecipeEntry> hits;
    std::uint64_t registryRevision = 0;
};

class RecipeRouter
{
  public:
    void setRecipes( std::vector<RecipeDocument> recipes );
    void clear();
    void setRegistryRevision( std::uint64_t revision );
    std::uint64_t registryRevision() const { return mRevision; }
    std::string packDigest() const;
    RecipeRouterResult search( const RecipeQuery &query ) const;
    static RecipeDocument fromRecipeJson( const Json::Value &doc );

  private:
    std::vector<RecipeDocument> mRecipes;
    std::uint64_t mRevision = 0;
};

} // namespace sicnu::science_context

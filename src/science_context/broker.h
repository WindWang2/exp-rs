// src/science_context/broker.h
#pragma once

#include "science_context/asset_state_provider.h"
#include "science_context/bundle.h"
#include "science_context/context_budget.h"
#include "science_context/context_cache.h"
#include "science_context/observability.h"
#include "science_context/recipe_router.h"
#include "science_context/planner_projection.h"

#include <string>
#include <vector>

namespace sicnu::science_context {

struct SynthesizeRequest
{
    std::string goal;
    std::string intent; ///< optional override
    std::vector<std::string> assetKeys;
    /// Optional pre-resolved passports (skips resolver for those keys).
    std::vector<sicnu::state::RemoteSensingAssetState> passports;
    ContextConstraints constraints;
    BudgetPolicy budget;
    bool useCache = true;
};

struct SynthesizeResult
{
    ScientificContextBundle bundle;
    bool cacheHit = false;
    PlanningContext planning; ///< filled via planner_projection.h
};

/// Orchestrates Asset → Capability → Recipe → Bundle → Planner projection.
class ScienceContextBroker
{
  public:
    AssetStateProvider &assets() { return mAssets; }
    const AssetStateProvider &assets() const { return mAssets; }
    RecipeRouter &recipes() { return mRecipes; }
    const RecipeRouter &recipes() const { return mRecipes; }
    ContextCache &cache() { return mCache; }
    BrokerObservability &observability() { return mObs; }

    SynthesizeResult synthesize( const SynthesizeRequest &request );

  private:
    AssetStateProvider mAssets;
    RecipeRouter mRecipes;
    ContextCache mCache;
    BrokerObservability mObs;
};

} // namespace sicnu::science_context

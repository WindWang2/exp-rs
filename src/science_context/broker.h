// src/science_context/broker.h
#pragma once

#include "science_context/asset_state_provider.h"
#include "science_context/bundle.h"
#include "science_context/capability_facts.h"
#include "science_context/context_budget.h"
#include "science_context/context_cache.h"
#include "science_context/observability.h"
#include "science_context/recipe_router.h"
#include "science_context/planner_projection.h"

#include <optional>
#include <string>
#include <vector>

namespace sicnu::recipes {
class ScientificRecipeRegistry;
}

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
/// The broker is a bounded projection of the real authorities
/// (scientific_state passport chain, ScientificRecipeRegistry, capability
/// knowledge via CapabilityFactsLookup) — never a second store.
class ScienceContextBroker
{
  public:
    AssetStateProvider &assets() { return mAssets; }
    const AssetStateProvider &assets() const { return mAssets; }
    RecipeRouter &recipes() { return mRecipes; }
    const RecipeRouter &recipes() const { return mRecipes; }
    ContextCache &cache() { return mCache; }
    BrokerObservability &observability() { return mObs; }

    /// Live capability authority (installed by the embedding layer).
    void setCapabilityFacts( CapabilityFactsLookup lookup );
    const CapabilityFactsLookup *capabilityFacts() const
    {
        return mCapFacts ? &*mCapFacts : nullptr;
    }

    /// Registry the broker syncs recipe projections from (not owned).
    void setRecipeRegistry( sicnu::recipes::ScientificRecipeRegistry *registry );
    sicnu::recipes::ScientificRecipeRegistry *recipeRegistry() const { return mRecipeRegistry; }

    /// Reload the registry into the recipe router. Returns false when the
    /// registry is unavailable (router left empty — fail-closed).
    bool refreshRecipes();

    // Mutation seams: invalidate projections derived from mutated authority.
    void invalidateAsset( const std::string &assetKey );
    void invalidateAllAssets();
    void notifyProjectSwitch();

    SynthesizeResult synthesize( const SynthesizeRequest &request );

  private:
    AssetStateProvider mAssets;
    RecipeRouter mRecipes;
    ContextCache mCache;
    BrokerObservability mObs;
    sicnu::recipes::ScientificRecipeRegistry *mRecipeRegistry = nullptr;
    std::optional<CapabilityFactsLookup> mCapFacts;
};

} // namespace sicnu::science_context

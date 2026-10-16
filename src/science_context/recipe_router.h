// src/science_context/recipe_router.h
#pragma once

#include "science_context/bundle.h"
#include <json/json.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::recipes {
class ScientificRecipeRegistry;
}

namespace sicnu::science_context {

/// How the router's current document set was produced. The registry is the
/// only store; `setRecipes` (manual seeding) is an explicit caller injection
/// and is reported as such in bundle provenance.
enum class RecipeSourceMode
{
    None,           ///< nothing loaded
    InlineInput,    ///< seeded via setRecipes (caller-owned docs)
    LiveAuthority   ///< synced from a ScientificRecipeRegistry
};

struct RecipeSourceInfo
{
    RecipeSourceMode mode = RecipeSourceMode::None;
    std::string authority;   ///< "recipes.scientific_recipe_registry" | "manual_seed"
    std::uint64_t revision = 0;      ///< content-derived when registry-synced
    std::string registryStatus;      ///< "ok" | "degraded" | "unavailable" at sync
    int registryProblems = 0;        ///< registry loadProblems at sync
};

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
    /// std::mutex is neither copyable nor movable, but the broker's
    /// function-local-static init (agent_adapter's sharedBroker) move-
    /// constructs the whole broker — so the router moves its PAYLOAD under
    /// both locks instead of its synchronization primitive.
    RecipeRouter() = default;
    RecipeRouter( RecipeRouter &&other ) noexcept;
    RecipeRouter &operator=( RecipeRouter &&other ) noexcept;
    RecipeRouter( const RecipeRouter & ) = delete;
    RecipeRouter &operator=( const RecipeRouter & ) = delete;

    void setRecipes( std::vector<RecipeDocument> recipes );
    void clear();
    void setRegistryRevision( std::uint64_t revision );
    std::uint64_t registryRevision() const;
    std::string packDigest() const;
    RecipeRouterResult search( const RecipeQuery &query ) const;
    static RecipeDocument fromRecipeJson( const Json::Value &doc );

    /// Replaces the docs with the registry's current contents (the registry
    /// stays the only store; this is a bounded projection). Reloads first.
    /// Returns false — and clears the docs — when the registry is
    /// unavailable (fail-closed, never stale leftovers).
    bool loadFromRegistry( sicnu::recipes::ScientificRecipeRegistry &registry );

    /// How the current document set was produced (bundle provenance).
    /// `revision` is the pack digest when registry-synced.
    RecipeSourceInfo sourceInfo() const;

    /// Revision and pack digest read under ONE lock — the broker's cache
    /// key needs them as a consistent pair ("cache key and provenance must
    /// agree even if the authority reloads mid-flight", R14-E review 7).
    std::pair<std::uint64_t, std::string> revisionAndDigest() const;

  private:
    // Caller must hold mMutex. Non-recursive helpers so the locked public
    // entry points can compose without recursive locking.
    void setRecipesLocked( std::vector<RecipeDocument> recipes );
    std::string packDigestLocked() const;

    // The broker composes this router with its own synchronized members but
    // historically documented no single-thread contract: the agent path can
    // refreshRecipes() (registry reload → vector replace) while another
    // thread is inside synthesize()'s search() iteration. One mutex makes
    // the router safe to read and replace concurrently (R14-E audit F5).
    mutable std::mutex mMutex;
    std::vector<RecipeDocument> mRecipes;
    std::uint64_t mRevision = 0;
    RecipeSourceInfo mSource;
};

} // namespace sicnu::science_context

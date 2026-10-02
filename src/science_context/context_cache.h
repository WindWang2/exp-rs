// src/science_context/context_cache.h
#pragma once
#include "science_context/bundle.h"
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace sicnu::science_context {

struct CacheKeyMaterial
{
    std::string assetDigest;
    std::uint64_t catalogGeneration = 0;
    std::uint64_t registryRevision = 0;
    std::string recipePackDigest;
    std::string autonomyRevision;
    std::string goal;
    std::string intent;
    bool offline = false;
    // Budget/constraint policy: bundles are budget-shaped, so two requests
    // differing here must never share an entry.
    int maxBytes = 0;
    int maxRecipes = 0;
    int maxCapabilities = 0;
    int maxOpenQuestions = 0;
    int maxAssets = 0;
    bool determinismRequired = true;
    // Capability facts authority ("" when the router runs on its builtin table).
    std::string capabilityAuthority;
    std::uint64_t capabilityRevision = 0;
};

std::string makeCacheKey( const CacheKeyMaterial &material );

class ContextCache
{
  public:
    ContextCache() = default;
    ContextCache( const ContextCache & ) = delete;             // iterators into mLru
    ContextCache &operator=( const ContextCache & ) = delete;
    ContextCache( ContextCache &&other ) noexcept;
    ContextCache &operator=( ContextCache &&other ) noexcept;

    /// Hard entry bound: inserting past it evicts the least-recently-used
    /// entry (the cache is a bounded projection, never a second store).
    static constexpr std::size_t kMaxEntries = 128;

    void put( const std::string &key, const ScientificContextBundle &bundle );
    std::optional<ScientificContextBundle> get( const std::string &key );
    void invalidate( const std::string &key );
    void clear();
    std::size_t size() const;
    std::size_t capacity() const { return kMaxEntries; }

    // Hit/miss/eviction counters — observability for invalidation-cost
    // probes; never part of bundle bytes.
    std::uint64_t hits() const;
    std::uint64_t misses() const;
    std::uint64_t evictions() const;

  private:
    void evictOverflow();

    struct Entry
    {
        ScientificContextBundle bundle;
        std::list<std::string>::iterator lruIt;
    };
    mutable std::mutex mMutex;
    std::unordered_map<std::string, Entry> mEntries;
    std::list<std::string> mLru; ///< front = most recently used
    std::uint64_t mHits = 0;
    std::uint64_t mMisses = 0;
    std::uint64_t mEvictions = 0;
};

} // namespace sicnu::science_context

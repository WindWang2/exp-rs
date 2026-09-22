// src/science_context/context_cache.h
#pragma once
#include "science_context/bundle.h"
#include <cstdint>
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
};

std::string makeCacheKey( const CacheKeyMaterial &material );

class ContextCache
{
  public:
    void put( const std::string &key, const ScientificContextBundle &bundle );
    std::optional<ScientificContextBundle> get( const std::string &key ) const;
    void invalidate( const std::string &key );
    void clear();
    std::size_t size() const { return mEntries.size(); }
  private:
    std::unordered_map<std::string, ScientificContextBundle> mEntries;
};

} // namespace sicnu::science_context

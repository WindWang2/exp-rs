// src/science_context/context_cache.cpp
#include "science_context/context_cache.h"

#include <cstdint>
#include <sstream>

namespace sicnu::science_context {

namespace {

std::uint64_t fnv1a64( const std::string &s )
{
    std::uint64_t h = 14695981039346656037ull;
    for ( unsigned char c : s )
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string hex16( std::uint64_t v )
{
    static const char *kHex = "0123456789abcdef";
    std::string out( 16, '0' );
    for ( int i = 15; i >= 0; --i )
    {
        out[static_cast<std::size_t>( i )] = kHex[v & 0xf];
        v >>= 4;
    }
    return out;
}

} // namespace

std::string makeCacheKey( const CacheKeyMaterial &material )
{
    std::ostringstream oss;
    oss << material.assetDigest << '|' << material.catalogGeneration << '|'
        << material.registryRevision << '|' << material.recipePackDigest << '|'
        << material.autonomyRevision << '|' << material.goal << '|' << material.intent
        << '|' << ( material.offline ? '1' : '0' )
        << '|' << material.maxBytes << '|' << material.maxRecipes
        << '|' << material.maxCapabilities << '|' << material.maxOpenQuestions
        << '|' << material.maxAssets
        << '|' << ( material.determinismRequired ? '1' : '0' )
        << '|' << material.capabilityAuthority << '|' << material.capabilityRevision;
    return hex16( fnv1a64( oss.str() ) );
}

void ContextCache::put( const std::string &key, const ScientificContextBundle &bundle )
{
    auto it = mEntries.find( key );
    if ( it != mEntries.end() )
    {
        it->second.bundle = bundle;
        mLru.splice( mLru.begin(), mLru, it->second.lruIt );
        return;
    }
    mLru.push_front( key );
    mEntries[key] = Entry{ bundle, mLru.begin() };
    evictOverflow();
}

std::optional<ScientificContextBundle> ContextCache::get( const std::string &key )
{
    auto it = mEntries.find( key );
    if ( it == mEntries.end() )
    {
        ++mMisses;
        return std::nullopt;
    }
    ++mHits;
    mLru.splice( mLru.begin(), mLru, it->second.lruIt );
    return it->second.bundle;
}

void ContextCache::invalidate( const std::string &key )
{
    auto it = mEntries.find( key );
    if ( it == mEntries.end() )
        return;
    mLru.erase( it->second.lruIt );
    mEntries.erase( it );
}

void ContextCache::clear()
{
    mEntries.clear();
    mLru.clear();
}

void ContextCache::evictOverflow()
{
    while ( mEntries.size() > kMaxEntries )
    {
        // Deterministic eviction: least-recently-used by access order, never
        // unordered_map iteration order. The list node and the map entry are
        // the SAME element — remove them once.
        auto victim = mEntries.find( mLru.back() );
        mLru.pop_back();
        mEntries.erase( victim );
        ++mEvictions;
    }
}

} // namespace sicnu::science_context

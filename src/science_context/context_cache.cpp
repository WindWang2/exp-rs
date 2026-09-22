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
        << '|' << ( material.offline ? '1' : '0' );
    return hex16( fnv1a64( oss.str() ) );
}

void ContextCache::put( const std::string &key, const ScientificContextBundle &bundle )
{
    mEntries[key] = bundle;
}

std::optional<ScientificContextBundle> ContextCache::get( const std::string &key ) const
{
    auto it = mEntries.find( key );
    if ( it == mEntries.end() )
        return std::nullopt;
    return it->second;
}

void ContextCache::invalidate( const std::string &key )
{
    mEntries.erase( key );
}

void ContextCache::clear()
{
    mEntries.clear();
}

} // namespace sicnu::science_context

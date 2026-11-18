// src/science_context/context_cache.cpp
#include "science_context/context_cache.h"

#include <cstdint>
#include <mutex>

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
    // Length-prefixed framing: the old raw '|' concatenation let
    // goal="a|b",intent="c" collide with goal="a",intent="b|c" — a hit then
    // returned a bundle synthesized for a DIFFERENT request. Every string
    // field is framed as length ':' bytes; numeric and bool fields are
    // single-character or digit runs that can never contain the framing
    // bytes, so their textual form stays unambiguous. The encoding change
    // naturally misses every entry cached under the old key scheme.
    std::string preimage;
    preimage.reserve( 128 );
    const auto appendStr = [ & ]( const std::string &s )
    {
        preimage += std::to_string( s.size() );
        preimage += ':';
        preimage += s;
        preimage += '|';
    };
    appendStr( material.assetDigest );
    preimage += std::to_string( material.catalogGeneration );
    preimage += '|';
    preimage += std::to_string( material.registryRevision );
    preimage += '|';
    appendStr( material.recipePackDigest );
    appendStr( material.autonomyRevision );
    appendStr( material.goal );
    appendStr( material.intent );
    preimage += ( material.offline ? '1' : '0' );
    preimage += '|';
    preimage += std::to_string( material.maxBytes );
    preimage += '|';
    preimage += std::to_string( material.maxRecipes );
    preimage += '|';
    preimage += std::to_string( material.maxCapabilities );
    preimage += '|';
    preimage += std::to_string( material.maxOpenQuestions );
    preimage += '|';
    preimage += std::to_string( material.maxAssets );
    preimage += '|';
    preimage += ( material.determinismRequired ? '1' : '0' );
    preimage += '|';
    appendStr( material.capabilityAuthority );
    preimage += std::to_string( material.capabilityRevision );
    preimage += '|';
    appendStr( material.failureDigest );
    return hex16( fnv1a64( preimage ) );
}

ContextCache::ContextCache( ContextCache &&other ) noexcept
{
    std::lock_guard<std::mutex> lock( other.mMutex );
    mEntries = std::move( other.mEntries );
    mLru = std::move( other.mLru );
    mHits = other.mHits;
    mMisses = other.mMisses;
    mEvictions = other.mEvictions;
}

ContextCache &ContextCache::operator=( ContextCache &&other ) noexcept
{
    if ( this != &other )
    {
        std::scoped_lock lock( mMutex, other.mMutex );
        mEntries = std::move( other.mEntries );
        mLru = std::move( other.mLru );
        mHits = other.mHits;
        mMisses = other.mMisses;
        mEvictions = other.mEvictions;
    }
    return *this;
}

void ContextCache::put( const std::string &key, const ScientificContextBundle &bundle )
{
    std::lock_guard<std::mutex> lock( mMutex );
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
    std::lock_guard<std::mutex> lock( mMutex );
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
    std::lock_guard<std::mutex> lock( mMutex );
    auto it = mEntries.find( key );
    if ( it == mEntries.end() )
        return;
    mLru.erase( it->second.lruIt );
    mEntries.erase( it );
}

void ContextCache::clear()
{
    std::lock_guard<std::mutex> lock( mMutex );
    mEntries.clear();
    mLru.clear();
}

std::size_t ContextCache::size() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mEntries.size();
}

std::uint64_t ContextCache::hits() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mHits;
}

std::uint64_t ContextCache::misses() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mMisses;
}

std::uint64_t ContextCache::evictions() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mEvictions;
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

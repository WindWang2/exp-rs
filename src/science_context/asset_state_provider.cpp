// src/science_context/asset_state_provider.cpp
#include "science_context/asset_state_provider.h"

#include "scientific_state/asset_state_types.h"

#include <algorithm>
#include <mutex>
#include <set>

namespace sicnu::science_context {

namespace {

using sicnu::state::ClaimKind;
using sicnu::state::claimFor;

EvidenceBucket dominantEvidence( const sicnu::state::RemoteSensingAssetState &state,
                                 std::vector<std::string> &paths,
                                 std::vector<std::string> &alts )
{
    bool conflicted = false;
    bool known = false;
    bool assumed = false;
    bool unknown = state.claims.empty();
    for ( const auto &c : state.claims )
    {
        paths.push_back( c.path );
        if ( c.kind == ClaimKind::Conflicted )
        {
            conflicted = true;
            for ( const auto &a : c.alternatives )
                alts.push_back( a );
        }
        else if ( c.kind == ClaimKind::Known || c.kind == ClaimKind::Inferred )
            known = true;
        else if ( c.kind == ClaimKind::Assumed )
            assumed = true;
        else if ( c.kind == ClaimKind::Unknown )
            unknown = true;
    }
    std::sort( paths.begin(), paths.end() );
    paths.erase( std::unique( paths.begin(), paths.end() ), paths.end() );
    std::sort( alts.begin(), alts.end() );
    alts.erase( std::unique( alts.begin(), alts.end() ), alts.end() );

    if ( conflicted )
        return EvidenceBucket::Conflicted;
    if ( known )
        return EvidenceBucket::Known;
    if ( assumed )
        return EvidenceBucket::Assumed;
    (void) unknown;
    return EvidenceBucket::Unknown;
}

} // namespace

std::string redactPathHint( const std::string &path )
{
    if ( path.empty() )
        return {};
    const auto pos = path.find_last_of( "/\\" );
    if ( pos == std::string::npos )
        return path;
    return path.substr( pos + 1 );
}

AssetStateProvider::AssetStateProvider( AssetStateProvider &&other ) noexcept
{
    std::lock_guard<std::mutex> lock( other.mMutex );
    mResolver = std::move( other.mResolver );
    mResolverAuthority = std::move( other.mResolverAuthority );
    mCache = std::move( other.mCache );
    mCatalogGeneration = other.mCatalogGeneration;
}

AssetStateProvider &AssetStateProvider::operator=( AssetStateProvider &&other ) noexcept
{
    if ( this != &other )
    {
        std::scoped_lock lock( mMutex, other.mMutex );
        mResolver = std::move( other.mResolver );
        mResolverAuthority = std::move( other.mResolverAuthority );
        mCache = std::move( other.mCache );
        mCatalogGeneration = other.mCatalogGeneration;
    }
    return *this;
}

void AssetStateProvider::setResolver( PassportResolver resolver )
{
    std::lock_guard<std::mutex> lock( mMutex );
    mResolver = std::move( resolver );
}

void AssetStateProvider::setResolverAuthority( const std::string &authority )
{
    std::lock_guard<std::mutex> lock( mMutex );
    mResolverAuthority = authority;
}

std::string AssetStateProvider::resolverAuthority() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mResolverAuthority;
}

bool AssetStateProvider::hasResolver() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return static_cast<bool>( mResolver );
}

void AssetStateProvider::clearCache()
{
    std::lock_guard<std::mutex> lock( mMutex );
    mCache.clear();
}

void AssetStateProvider::clear()
{
    clearCache();
}

void AssetStateProvider::invalidate( const std::string &assetKey )
{
    std::lock_guard<std::mutex> lock( mMutex );
    mCache.erase( assetKey );
}

void AssetStateProvider::invalidateAll()
{
    std::lock_guard<std::mutex> lock( mMutex );
    mCache.clear();
}

std::uint64_t AssetStateProvider::catalogGeneration() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mCatalogGeneration;
}

void AssetStateProvider::setCatalogGeneration( std::uint64_t generation )
{
    std::lock_guard<std::mutex> lock( mMutex );
    if ( generation != mCatalogGeneration )
    {
        mCatalogGeneration = generation;
        mCache.clear();
    }
}

AssetSummary AssetStateProvider::summarize( const sicnu::state::RemoteSensingAssetState &state )
{
    AssetSummary s;
    s.assetId = state.assetId;
    s.revision = state.revision;
    s.displayName = state.displayName;
    s.modality = sicnu::state::modalityToString( state.sensor.modality );
    // Conflicted radiometric: do not auto-pick a unit.
    const auto rad = claimFor( state, "radiometric.unit" );
    if ( rad.kind == ClaimKind::Conflicted )
    {
        s.radiometricUnit.clear();
        s.conflictAlternatives = rad.alternatives;
    }
    else if ( rad.kind == ClaimKind::Unknown )
    {
        s.radiometricUnit.clear(); // unknown ≠ default
    }
    else
    {
        s.radiometricUnit = state.radiometric.unit;
    }
    if ( state.geometry.hasCrs )
        s.crsAuthid = state.geometry.crsAuthid;
    std::set<std::string> roles;
    for ( const auto &b : state.bands )
    {
        if ( !b.role.empty() )
            roles.insert( b.role );
    }
    s.bandRoles.assign( roles.begin(), roles.end() );
    s.evidence = dominantEvidence( state, s.evidencePaths, s.conflictAlternatives );
    s.pathHint = redactPathHint( state.sourcePath );
    return s;
}

AssetResolveResult AssetStateProvider::resolve( const AssetResolveRequest &request ) const
{
    AssetResolveResult result;
    if ( request.assetKey.empty() )
    {
        result.error = "asset_key_required";
        return result;
    }

    PassportResolver resolver;
    sicnu::state::RemoteSensingAssetState state;
    bool cached = false;
    {
        std::lock_guard<std::mutex> lock( mMutex );
        auto it = mCache.find( request.assetKey );
        if ( it != mCache.end() )
        {
            state = it->second;
            cached = true;
        }
        else
        {
            resolver = mResolver;
        }
    }

    if ( !cached )
    {
        if ( !resolver )
        {
            result.error = "resolver_unavailable";
            return result;
        }

        const PassportResolution resolved = resolver( request.assetKey );
        if ( !resolved )
        {
            // Typed failure: keep the resolver's reason (gdal_open_failed vs
            // asset_not_found vs ...). Failures are never cached — the next
            // resolve re-consults the authority.
            result.error = resolved.errorCode.empty() ? "asset_not_found" : resolved.errorCode;
            result.errorDetail = resolved.errorDetail;
            return result;
        }

        state = *resolved.state;
        // The cache stores the FULL resolution: it is keyed by assetKey only
        // (there is no allowAssumed dimension), so the caller's strictness is
        // a projection applied on the way OUT, never baked into the entry.
        // Baking it in let whichever caller resolved FIRST define the facts
        // every later caller saw — a strict preflight:check inheriting a
        // lenient entry's assumed claims (or the reverse).
        {
            std::lock_guard<std::mutex> lock( mMutex );
            mCache[request.assetKey] = state;
        }
    }

    // Strip assumed claims when the caller disallows them. Applied to the
    // RETURNED projection only (cache hit and miss alike) so strict and
    // lenient callers can never pollute each other through the shared cache.
    if ( !request.allowAssumed )
    {
        auto &claims = state.claims;
        claims.erase( std::remove_if( claims.begin(), claims.end(),
                                      []( const sicnu::state::ClaimRecord &c ) {
                                          return c.kind == ClaimKind::Assumed;
                                      } ),
                      claims.end() );
    }

    result.ok = true;
    result.state = std::move( state );
    result.summary = summarize( result.state );
    return result;
}

} // namespace sicnu::science_context

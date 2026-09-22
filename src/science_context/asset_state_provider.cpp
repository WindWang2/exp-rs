// src/science_context/asset_state_provider.cpp
#include "science_context/asset_state_provider.h"

#include "scientific_state/asset_state_types.h"

#include <algorithm>
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

void AssetStateProvider::setResolver( PassportResolver resolver )
{
    mResolver = std::move( resolver );
}

void AssetStateProvider::clearCache()
{
    mCache.clear();
}

void AssetStateProvider::invalidate( const std::string &assetKey )
{
    mCache.erase( assetKey );
}

void AssetStateProvider::invalidateAll()
{
    mCache.clear();
}

void AssetStateProvider::setCatalogGeneration( std::uint64_t generation )
{
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

    auto it = mCache.find( request.assetKey );
    if ( it != mCache.end() )
    {
        result.ok = true;
        result.state = it->second;
        result.summary = summarize( result.state );
        return result;
    }

    if ( !mResolver )
    {
        result.error = "resolver_unavailable";
        return result;
    }

    auto resolved = mResolver( request.assetKey );
    if ( !resolved )
    {
        result.error = "asset_not_found";
        return result;
    }

    // Strip assumed claims when caller disallows them.
    if ( !request.allowAssumed )
    {
        auto &claims = resolved->claims;
        claims.erase( std::remove_if( claims.begin(), claims.end(),
                                      []( const sicnu::state::ClaimRecord &c ) {
                                          return c.kind == ClaimKind::Assumed;
                                      } ),
                      claims.end() );
    }

    mCache[request.assetKey] = *resolved;
    result.ok = true;
    result.state = *resolved;
    result.summary = summarize( result.state );
    return result;
}

} // namespace sicnu::science_context

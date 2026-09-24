// src/science_context/asset_state_provider.h
#pragma once

#include "science_context/bundle.h"
#include "scientific_state/asset_state_types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace sicnu::science_context {

using PassportResolver = std::function<std::optional<sicnu::state::RemoteSensingAssetState>(
    const std::string &assetKey )>;

struct AssetResolveRequest
{
    std::string assetKey;
    bool allowAssumed = true;
};

struct AssetResolveResult
{
    bool ok = false;
    AssetSummary summary;
    sicnu::state::RemoteSensingAssetState state;
    std::string error;
};

class AssetStateProvider
{
  public:
    void setResolver( PassportResolver resolver );
    /// Bundle-provenance id of the wired resolver ("" when none wired).
    void setResolverAuthority( const std::string &authority ) { mResolverAuthority = authority; }
    const std::string &resolverAuthority() const { return mResolverAuthority; }
    bool hasResolver() const { return static_cast<bool>( mResolver ); }
    void clearCache();
    void invalidate( const std::string &assetKey );
    void invalidateAll();
    void setCatalogGeneration( std::uint64_t generation );
    std::uint64_t catalogGeneration() const { return mCatalogGeneration; }
    AssetResolveResult resolve( const AssetResolveRequest &request ) const;
    static AssetSummary summarize( const sicnu::state::RemoteSensingAssetState &state );

  private:
    PassportResolver mResolver;
    std::string mResolverAuthority;
    mutable std::unordered_map<std::string, sicnu::state::RemoteSensingAssetState> mCache;
    std::uint64_t mCatalogGeneration = 0;
};

std::string redactPathHint( const std::string &path );

} // namespace sicnu::science_context

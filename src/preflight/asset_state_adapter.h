// asset_state_adapter.h — scientific_state → preflight facts projection.
//
// The provider seam's scientific_state side: a RemoteSensingAssetState (the
// RS14-01 data passport) projects read-only into SlotFacts. The passport
// stays the single truth source — this adapter only maps fields, it never
// re-derives meanings the resolver owns. Unresolved references surface as
// FactStatus::Unknown; nothing is fabricated.
//
// Note on temporal facts: the passport carries temporal collection
// references but not scene counts/dates; a provider with catalog access
// should supply those. projectAssetState leaves them zero/empty (typed
// unknown downstream), never guessed.

#pragma once

#include "preflight/facts.h"
#include "preflight/provider.h"

#include "scientific_state/asset_state_types.h"

#include <functional>
#include <optional>
#include <string>

namespace sicnu::preflight {

/// Pure field projection: passport → slot facts (slot/assetRef filled by the
/// caller or the engine).
SlotFacts projectAssetState( const sicnu::state::RemoteSensingAssetState &state );

/// IAssetFactsProvider over a passport resolver — the same resolver shape as
/// sicnu::science_context::AssetStateProvider, so deployments can share one
/// authority. A resolver that returns nullopt yields FactStatus::Unknown.
class StateAssetFactsProvider : public IAssetFactsProvider
{
  public:
    using Resolver =
        std::function<std::optional<sicnu::state::RemoteSensingAssetState>( const std::string &assetRef )>;

    explicit StateAssetFactsProvider( Resolver resolver );

    SlotFactsResult slotFacts( const std::string &assetRef ) const override;

  private:
    Resolver resolver_;
};

} // namespace sicnu::preflight

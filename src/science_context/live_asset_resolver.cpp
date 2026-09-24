// src/science_context/live_asset_resolver.cpp
#include "science_context/live_asset_resolver.h"

#include "scientific_state/asset_state_resolver.h"

namespace sicnu::science_context {

PassportResolver makeFactsBasedResolver( AssetFactSources sources )
{
    return [sources = std::move( sources )]( const std::string &assetKey )
        -> std::optional<sicnu::state::RemoteSensingAssetState> {
        sicnu::state::StateResolutionInput input;
        bool anyFacts = false;
        if ( sources.catalog )
        {
            input.catalog = sources.catalog( assetKey );
            anyFacts = anyFacts || input.catalog.has_value();
        }
        if ( sources.dataset )
        {
            input.dataset = sources.dataset( assetKey );
            anyFacts = anyFacts || input.dataset.has_value();
        }
        if ( sources.sensorProfile )
        {
            input.sensorProfile = sources.sensorProfile( assetKey );
            anyFacts = anyFacts || input.sensorProfile.has_value();
        }
        if ( sources.derivation )
        {
            input.derivation = sources.derivation( assetKey );
            anyFacts = anyFacts || input.derivation.has_value();
        }
        if ( sources.modelSidecar )
        {
            input.modelSidecar = sources.modelSidecar( assetKey );
            anyFacts = anyFacts || input.modelSidecar.has_value();
        }
        if ( !anyFacts )
            return std::nullopt; // nothing known for this key — fail closed
        return resolveAssetState( input ).state;
    };
}

} // namespace sicnu::science_context

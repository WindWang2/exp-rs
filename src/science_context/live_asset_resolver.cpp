// src/science_context/live_asset_resolver.cpp
#include "science_context/live_asset_resolver.h"

#include "scientific_state/asset_state_resolver.h"

namespace sicnu::science_context {

PassportResolver makeFactsBasedResolver( AssetFactSources sources )
{
    return [sources = std::move( sources )]( const std::string &assetKey )
        -> PassportResolution {
        sicnu::state::StateResolutionInput input;
        bool anyFacts = false;
        std::string errorCode;
        std::string errorDetail;
        if ( sources.catalog )
        {
            input.catalog = sources.catalog( assetKey );
            anyFacts = anyFacts || input.catalog.has_value();
        }
        if ( sources.dataset )
        {
            DatasetFactsLookup lookup = sources.dataset( assetKey );
            input.dataset = lookup.facts;
            anyFacts = anyFacts || input.dataset.has_value();
            // First typed reason wins: "GDAL could not open" must reach the
            // bundle even when other sources are simply silent about the key.
            if ( !lookup.facts && errorCode.empty() && !lookup.errorCode.empty() )
            {
                errorCode = lookup.errorCode;
                errorDetail = lookup.errorDetail;
            }
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
        {
            // Fail closed with the typed reason when one source reported why;
            // a plain "nothing known" stays asset_not_found.
            return PassportResolution{ std::nullopt, errorCode, errorDetail };
        }
        return PassportResolution{ resolveAssetState( input ).state, std::string(),
                                   std::string() };
    };
}

} // namespace sicnu::science_context

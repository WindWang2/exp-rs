#pragma once
// src/science_context/live_asset_resolver.h

//
// Live passport chain for the broker: composes injectable fact sources and
// the *real* scientific_state authority (resolveAssetState). The claim
// lattice (known/inferred/assumed/unknown/conflicted) flows through exactly
// as the passport authority resolved it — this adapter never rewrites
// evidence, downgrades claims, or fabricates facts. When no source yields
// facts for a key the resolver fails closed, carrying the first typed source
// error (e.g. gdal_open_failed) so the reason survives to the bundle; only a
// source that plainly has nothing becomes asset_not_found.
//

#include "science_context/asset_state_provider.h"
#include "scientific_state/state_facts.h"

#include <functional>
#include <optional>
#include <string>

namespace sicnu::science_context {

/// Typed dataset-facts lookup: facts absent ⇒ errorCode says why
/// ("" = the source simply has nothing for this key).
struct DatasetFactsLookup
{
    std::optional<sicnu::state::DatasetFacts> facts;
    std::string errorCode;
    std::string errorDetail;
};

/// Per-key fact sources; nullopt = that authority has nothing for the key.
struct AssetFactSources
{
    std::function<std::optional<sicnu::state::CatalogFacts>( const std::string & )> catalog;
    std::function<DatasetFactsLookup( const std::string & )> dataset;
    std::function<std::optional<sicnu::state::SensorProfileFacts>( const std::string & )>
        sensorProfile;
    std::function<std::optional<sicnu::state::DerivationFacts>( const std::string & )>
        derivation;
    std::function<std::optional<sicnu::state::ModelSidecarFacts>( const std::string & )>
        modelSidecar;

    /// Bundle-provenance authority id for passports resolved through this chain.
    std::string authority = "scientific_state.resolve_asset_state";
};

PassportResolver makeFactsBasedResolver( AssetFactSources sources );

} // namespace sicnu::science_context

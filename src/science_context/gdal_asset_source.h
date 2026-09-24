// src/science_context/gdal_asset_source.h
#pragma once

//
// DatasetFacts collector over a real read-only GDAL open (scientific_state
// GDAL adapter). Compiled into sicnu_science_context only when
// Sicnu::ScientificStateGdal is available; embedders combine it with other
// AssetFactSources members via makeFactsBasedResolver.
//

#include "scientific_state/state_facts.h"

#include <functional>
#include <optional>
#include <string>

namespace sicnu::science_context {

/// Returns a source function that treats its argument as a file path and
/// performs ONE read-only GDAL open + metadata queries per call.
std::function<std::optional<sicnu::state::DatasetFacts>( const std::string &path )>
gdalDatasetFactsCollector();

} // namespace sicnu::science_context

// src/science_context/gdal_asset_source.cpp
#include "science_context/gdal_asset_source.h"

#include "scientific_state/gdal/gdal_state_facts.h"

namespace sicnu::science_context {

std::function<std::optional<sicnu::state::DatasetFacts>( const std::string &path )>
gdalDatasetFactsCollector()
{
    return []( const std::string &path ) -> std::optional<sicnu::state::DatasetFacts> {
        std::string error;
        return sicnu::state::collectDatasetFacts( path, error );
    };
}

} // namespace sicnu::science_context

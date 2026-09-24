// src/science_context/gdal_asset_source.cpp
#include "science_context/gdal_asset_source.h"

#include "science_context/live_asset_resolver.h"
#include "scientific_state/gdal/gdal_state_facts.h"

namespace sicnu::science_context {

std::function<DatasetFactsLookup( const std::string &path )> gdalDatasetFactsCollector()
{
    return []( const std::string &path ) -> DatasetFactsLookup {
        sicnu::state::GdalFactsError error;
        DatasetFactsLookup lookup;
        lookup.facts = sicnu::state::collectDatasetFacts( path, &error );
        if ( !lookup.facts )
        {
            lookup.errorCode = error.code;
            lookup.errorDetail = error.detail;
        }
        return lookup;
    };
}

} // namespace sicnu::science_context

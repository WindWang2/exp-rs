#pragma once

#include "../internal/source_provider.h"

namespace sicnu::data::providers
{

/// Source provider for local OGR vector files (GeoJSON, Shapefile, GPKG, ...).
///
/// Resolves structural metadata (layer + feature counts) through the GDAL/OGR
/// C API and normalizes the canonical identity (symlink resolution) so that the
/// same file reached through different path spellings deduplicates.
class OgrVectorSourceProvider final : public internal::SourceProvider
{
  public:
    bool supports( const SourceDescriptor &source ) const override;
    Result<internal::ResolvedSource> resolve( const SourceDescriptor &source ) const override;
};

} // namespace sicnu::data::providers

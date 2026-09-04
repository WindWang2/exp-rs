#pragma once

#include "../internal/source_provider.h"

namespace sicnu::data::providers
{

/// Source provider for local GDAL raster files (GeoTIFF, ENVI, and friends).
///
/// It probes by file extension and ENVI header siblings, resolves structural
/// metadata through the GDAL C API, and normalizes the canonical identity so
/// that an ENVI `.hdr` sidecar and its paired binary data file — as well as
/// symlinked or `../`-spelled paths — collapse onto a single SourceKey.
class GdalRasterSourceProvider final : public internal::SourceProvider
{
  public:
    bool supports( const SourceDescriptor &source ) const override;
    Result<internal::ResolvedSource> resolve( const SourceDescriptor &source ) const override;

    /// Catalog identity for a remote raster: prefixes a bare http(s) href with
    /// `/vsicurl/`. Other spellings (already-prefixed VSI paths, local files)
    /// are returned unchanged.
    static QString normalizeRemoteRasterSource( const QString &path );
};

} // namespace sicnu::data::providers

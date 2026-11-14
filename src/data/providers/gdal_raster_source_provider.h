#pragma once

#include "../internal/source_provider.h"

#include <QString>

namespace sicnu::data::providers
{

/// A parsed GDAL subdataset spelling — `DRIVER:"/path/container.ext":member`
/// (the shape GDAL reports as SUBDATASET_n_NAME and opens directly). One
/// parser serves the provider's supports/normalize paths AND the import
/// preflight's member dispatch (R1 review P3-4: two drifting copies are one
/// bug farm). `valid` requires a non-empty member selector.
struct SubdatasetSpelling
{
  bool valid = false;
  QString container;
  QString member;
};

SubdatasetSpelling parseSubdatasetSpelling( const QString &path );

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

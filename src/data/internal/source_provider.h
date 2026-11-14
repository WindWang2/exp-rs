#pragma once

#include <QString>

#include "../asset_types.h"
#include "../data_asset.h"
#include "../data_result.h"
#include "../source_descriptor.h"

namespace sicnu::geo
{
struct ConvertProgress;
}

namespace sicnu::data::internal
{

struct ResolvedSource
{
  AssetKind kind = AssetKind::Raster;
  AssetState state = AssetState::Ready;
  AssetCapabilities capabilities;
  StorageKind storageKind = StorageKind::File;
  QString displayName;
  /// Canonical, provider-normalized source location used for deduplication.
  /// For file-backed providers this is the resolved, symlink-followed absolute
  /// path (e.g. the ENVI binary data file, not its `.hdr` sidecar). Callers must
  /// use this — not the raw request string — as the SourceKey identity.
  QString canonicalSource;
  /// Stable provider identity used to collapse aliases and inferred providers.
  QString canonicalProviderKey;
  AssetStructure structure;
};

class SourceProvider
{
  public:
    virtual ~SourceProvider() = default;

    virtual bool supports( const SourceDescriptor &source ) const = 0;
    virtual Result<ResolvedSource> resolve( const SourceDescriptor &source ) const = 0;

    /// Progress-aware resolution (R30-T03 WP04): providers whose preparation
    /// runs long (the VRT provider's warp staging) report staged progress and
    /// honour cooperative cancellation through the geospatial foundation's
    /// shared ConvertProgress sink. The default implementation ignores the
    /// sink and resolves exactly like the 1-argument form, so providers
    /// without a long preparation need no change.
    ///
    /// Sink contract (R1 review P3-2): report() fires SYNCHRONOUSLY on the
    /// calling thread (GDAL warp runs single-threaded here); the sink and
    /// its cancelFlag must outlive the call; a callback must not re-enter
    /// the DataManager (mutations stay owning-thread bound).
    virtual Result<ResolvedSource> resolve( const SourceDescriptor &source,
                                            sicnu::geo::ConvertProgress *progress ) const
    {
      ( void )progress;
      return resolve( source );
    }
};

} // namespace sicnu::data::internal

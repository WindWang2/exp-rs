#pragma once

#include <QString>

#include "../asset_types.h"
#include "../data_result.h"
#include "../source_descriptor.h"

namespace sicnu::data::internal
{

struct ResolvedSource
{
  AssetKind kind = AssetKind::Raster;
  AssetState state = AssetState::Ready;
  AssetCapabilities capabilities;
  StorageKind storageKind = StorageKind::File;
  QString displayName;
};

class SourceProvider
{
  public:
    virtual ~SourceProvider() = default;

    virtual bool supports( const SourceDescriptor &source ) const = 0;
    virtual Result<ResolvedSource> resolve( const SourceDescriptor &source ) const = 0;
};

} // namespace sicnu::data::internal

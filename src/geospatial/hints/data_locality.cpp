/***************************************************************************
  geospatial/hints/data_locality.cpp — data locality & execution hints (M8).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/hints/data_locality.h"

#include "geospatial/identity/asset_identity.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/remote/remote_source_validator.h"
#include "geospatial/util/resource_uri.h"

#include <system_error>

#include <filesystem>

namespace sicnu::geo
{

Json::Value DataLocalityHints::toJson() const
{
  Json::Value json;
  json["path"] = path;
  json["kind"] = kind;
  json["locality"] = locality;
  if ( hasEstimatedBytes )
    json["estimated_bytes"] = static_cast<Json::UInt64>( estimatedBytes );
  json["seek_cost_low"] = seekCostLow;
  json["cog_optimized"] = cogOptimized;
  if ( !compression.empty() )
    json["compression"] = compression;
  if ( !preferredChunkShape.empty() )
  {
    Json::Value shape( Json::arrayValue );
    for ( const std::int64_t value : preferredChunkShape )
      shape.append( static_cast<Json::Int64>( value ) );
    json["preferred_chunk_shape"] = shape;
  }
  if ( !identityToken.empty() )
  {
    json["identity_token"] = identityToken;
    json["identity_strength"] = identityStrength;
  }
  if ( remoteProbed )
  {
    json["remote_probed"] = true;
    json["remote_accepts_ranges"] = remoteAcceptsRanges;
  }
  return json;
}

DataLocalityHints dataLocalityHintsFor( const std::string &path, const DataLocalityOptions &options )
{
  DataLocalityHints hints;
  hints.path = path;
  const ResourceUri uri = ResourceUri::parse( path );
  const bool remote = uri.isRemote();

  // Identity first (cheap for locals, a bounded probe for remotes): the
  // cacheability fact every consumer must respect ("" = not cacheable).
  AssetIdentityOptions identityOptions;
  identityOptions.timeoutSeconds = options.timeoutSeconds;
  identityOptions.connectTimeoutSeconds = options.connectTimeoutSeconds;
  identityOptions.maxRetries = options.maxRetries;
  identityOptions.probeBytes = options.remoteProbeBytes;
  const AssetIdentity identity = assetIdentityToken( path, identityOptions );
  hints.identityToken = identity.token;
  hints.identityStrength = identity.strength;

  const InspectOptions inspectOptions; // statistics stay OFF: hints never scan data

  // Remote resources: the bounded probe is the honest source of facts; the
  // layout story stays unknown without an open (never invented).
  if ( remote )
  {
    hints.locality = "remote";
    hints.kind = "remote";
    RemoteValidatorOptions validatorOptions;
    validatorOptions.timeoutSeconds = options.timeoutSeconds;
    validatorOptions.connectTimeoutSeconds = options.connectTimeoutSeconds;
    validatorOptions.maxRetries = options.maxRetries;
    validatorOptions.probeBytes = options.remoteProbeBytes;
    RemoteSourceValidator validator = RemoteSourceValidator::probe(
      uri.kind == ResourceKind::VsiRemote ? uri.remoteUrl() : path, validatorOptions );
    const RemoteSourceIdentity &remoteIdentity = validator.identity();
    hints.remoteProbed = remoteIdentity.state != RemoteSourceState::Offline;
    hints.remoteAcceptsRanges = remoteIdentity.acceptsRanges;
    if ( remoteIdentity.hasSize )
    {
      hints.hasEstimatedBytes = true;
      hints.estimatedBytes = remoteIdentity.sizeBytes;
    }
    return hints;
  }

  hints.locality = "local";
  // Dispatch by the same raster → vector → multidim ladder inspectAny uses;
  // a type that never opens stays "unknown" — no invented facts.
  try
  {
    const RasterMetadata metadata = inspectRaster( path, inspectOptions );
    hints.kind = "raster";
    if ( !metadata.compression.empty() )
      hints.compression = metadata.compression;
    RasterReader reader = RasterReader::open( path );
    const std::pair<int, int> block = reader.blockSize( 1 );
    const bool tiled = block.first >= 256 && block.second >= 256;
    hints.seekCostLow = tiled;
    hints.cogOptimized = metadata.driver == "COG" || ( tiled && metadata.driver == "GTiff" );
    if ( tiled )
      hints.preferredChunkShape = { block.second, block.first }; // slowest→fastest
  }
  catch ( const GeoError &rasterError )
  {
    if ( rasterError.code() != ErrorCode::OpenFailed )
      hints.kind = "unknown";
    else
      try
      {
        const VectorMetadata metadata = inspectVector( path, inspectOptions );
        hints.kind = "vector";
        hints.seekCostLow =
          !metadata.layers.empty() && metadata.layers.front().supportsFastSpatialFilter;
      }
      catch ( const GeoError &vectorError )
      {
        if ( vectorError.code() != ErrorCode::OpenFailed )
          hints.kind = "unknown";
        else
          try
          {
            const MultidimMetadata metadata = inspectMultidim( path, inspectOptions );
            hints.kind = "multidim";
            for ( const VariableInfo &variable : metadata.variables )
            {
              if ( !variable.blockShape.empty() )
              {
                hints.preferredChunkShape = variable.blockShape;
                hints.seekCostLow = true;
                break;
              }
            }
          }
          catch ( const GeoError & )
          {
            hints.kind = "unknown"; // unreadable: say nothing we cannot know
          }
      }
  }

  // Local estimated bytes: a plain stat.
  std::error_code ec;
  const auto size = std::filesystem::file_size( std::filesystem::u8path( path ), ec );
  if ( !ec )
  {
    hints.hasEstimatedBytes = true;
    hints.estimatedBytes = size;
  }
  return hints;
}

} // namespace sicnu::geo

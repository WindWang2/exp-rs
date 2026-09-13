/***************************************************************************
  geospatial/fabric/virtual_cube.cpp — virtual mosaic / time cube.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/fabric/virtual_cube.h"

#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/object_store.h"
#include "geospatial/identity/asset_identity.h"

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>

namespace sicnu::geo
{

namespace
{

/// North-up grid conventions everywhere in this module (D-1005): positive
/// X resolution, negative Y — callers hand raw magnitudes either way.
double positiveScale( double value )
{
  return value < 0.0 ? -value : value;
}

struct AssetNativeGrid
{
  bool ok = false;
  std::string authid;
  double resX = 0.0, resY = 0.0;
  double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
};

AssetNativeGrid nativeGridOf( const RasterMetadata &metadata )
{
  AssetNativeGrid grid;
  grid.authid = metadata.crs.authid;
  if ( metadata.width <= 0 || metadata.height <= 0 || !metadata.hasGeotransform )
    return grid;
  if ( metadata.geotransform[1] == 0.0 )
    return grid;
  const std::array<double, 6> &gt = metadata.geotransform;
  grid.resX = positiveScale( gt[1] );
  grid.resY = positiveScale( gt[5] );
  if ( grid.resX == 0.0 || grid.resY == 0.0 )
    return grid;
  grid.minX = gt[0] + ( gt[1] > 0 ? 0.0 : static_cast<double>( metadata.width ) * gt[1] );
  grid.maxX = gt[0] + ( gt[1] > 0 ? static_cast<double>( metadata.width ) * gt[1] : 0.0 );
  grid.minY = std::min( gt[3], gt[3] + static_cast<double>( metadata.height ) * gt[5] );
  grid.maxY = std::max( gt[3], gt[3] + static_cast<double>( metadata.height ) * gt[5] );
  grid.ok = true;
  return grid;
}

/// D-1007: deterministic quality order — (cloudCover asc, undeclared last),
/// (instant desc), (id asc), (input order). Total and stable.
std::vector<std::size_t> selectionOrder( const std::vector<AssetRecord> &assets,
                                         const VirtualCubeQuality &quality )
{
  std::vector<std::size_t> order( assets.size() );
  for ( std::size_t i = 0; i < order.size(); ++i )
    order[i] = i;
  // Epoch-nanos instants (never string ordering — same-second values with
  // different fraction shapes would order inversely as strings).
  std::vector<std::int64_t> instants( assets.size(), 0 );
  std::vector<bool> dated( assets.size(), false );
  for ( std::size_t i = 0; i < assets.size(); ++i )
  {
    const InstantParse parse = parseIso8601Instant( assets[i].datetimeUtc );
    if ( parse.ok )
    {
      instants[i] = parse.epochNanos;
      dated[i] = true;
    }
  }
  std::stable_sort( order.begin(), order.end(), [ & ]( std::size_t a, std::size_t b ) {
    if ( quality.byCloudCoverAscending )
    {
      const bool aHas = assets[a].hasCloudCover;
      const bool bHas = assets[b].hasCloudCover;
      if ( aHas != bHas )
        return aHas;   // declared cover first, undeclared last
      if ( aHas && assets[a].cloudCover != assets[b].cloudCover )
        return assets[a].cloudCover < assets[b].cloudCover;
    }
    if ( quality.byNewestFirst && dated[a] != dated[b] )
    {
      return static_cast<bool>( dated[a] );   // dated first, undated last
    }
    if ( quality.byNewestFirst && dated[a] && dated[b] && instants[a] != instants[b] )
      return instants[a] > instants[b];   // newest first, exact nanos
    if ( assets[a].id != assets[b].id )
      return assets[a].id < assets[b].id;
    return false;   // input order (stable_sort keeps it)
  } );
  return order;
}

/// The canonical band role resolver: a role match wins, the fixed index
/// falls back. Returns 0 when neither resolves (caller records failure).
int resolveBandIndex( const RasterMetadata &metadata, const VirtualCubeReadOptions &options,
                      std::string &how )
{
  if ( !options.bandRole.empty() )
  {
    for ( const BandInfo &band : metadata.bands )
      if ( band.role == options.bandRole )
      {
        how = "role:" + options.bandRole;
        return band.index;
      }
  }
  if ( options.bandIndex >= 1 && options.bandIndex <= static_cast<int>( metadata.bands.size() ) )
  {
    how = "index";
    return options.bandIndex;
  }
  return 0;
}

} // namespace

// --- grid ------------------------------------------------------------------

bool VirtualCubeGrid::valid() const
{
  return crs.valid && scaleX > 0.0 && scaleY > 0.0 && maxX > minX && maxY > minY;
}

int VirtualCubeGrid::width() const
{
  if ( !valid() )
    throw GeoError( ErrorCode::InvalidArgument, "cube grid is not valid" );
  return static_cast<int>( std::llround( ( maxX - minX ) / scaleX ) );
}

int VirtualCubeGrid::height() const
{
  if ( !valid() )
    throw GeoError( ErrorCode::InvalidArgument, "cube grid is not valid" );
  return static_cast<int>( std::llround( ( maxY - minY ) / scaleY ) );
}

Json::Value VirtualCubeGrid::toJson() const
{
  Json::Value json;
  json["explicit"] = explicitGrid;
  json["crs"] = crs.authid.empty() ? crs.wkt : crs.authid;
  json["scaleX"] = scaleX;
  json["scaleY"] = scaleY;
  Json::Value extent( Json::arrayValue );
  extent.append( minX );
  extent.append( minY );
  extent.append( maxX );
  extent.append( maxY );
  json["extent"] = extent;
  return json;
}

VirtualCubeGrid VirtualCubeGrid::fromJson( const Json::Value &json )
{
  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = !json["crs"].asString().empty();
  grid.crs.authid = json["crs"].asString();
  grid.scaleX = json["scaleX"].asDouble();
  grid.scaleY = json["scaleY"].asDouble();
  const Json::Value &extent = json["extent"];
  if ( extent.isArray() && extent.size() == 4 )
  {
    grid.minX = extent[0].asDouble();
    grid.minY = extent[1].asDouble();
    grid.maxX = extent[2].asDouble();
    grid.maxY = extent[3].asDouble();
  }
  if ( !grid.valid() )
    throw GeoError( ErrorCode::InvalidArgument, "virtual cube grid JSON is not a valid grid" );
  return grid;
}

const char *overlapPolicyName( OverlapPolicy policy )
{
  return policy == OverlapPolicy::FirstWins ? "first_wins" : "unknown";
}

OverlapPolicy overlapPolicyFromName( const std::string &name )
{
  if ( name == "first_wins" )
    return OverlapPolicy::FirstWins;
  throw GeoError( ErrorCode::InvalidArgument, "unknown overlap policy: " + name );
}

// --- provenance / result ----------------------------------------------------

Json::Value VirtualCubeProvenance::toJson() const
{
  Json::Value json;
  json["assetId"] = assetId;
  json["assetPath"] = ResourceUri::parse( assetPath ).display();
  json["instantUtc"] = instantUtc;
  json["sourceWindow"] = Json::Value( Json::arrayValue );
  json["sourceWindow"].append( sourceWindow.xOff );
  json["sourceWindow"].append( sourceWindow.yOff );
  json["sourceWindow"].append( sourceWindow.width );
  json["sourceWindow"].append( sourceWindow.height );
  Json::Value extent( Json::arrayValue );
  extent.append( targetMinX );
  extent.append( targetMinY );
  extent.append( targetMaxX );
  extent.append( targetMaxY );
  json["targetExtent"] = extent;
  json["contributed"] = contributed;
  json["failed"] = failed;
  if ( !errorText.empty() )
    json["error"] = errorText;
  if ( !mirrorHit.empty() )
    json["mirrorHit"] = ResourceUri::parse( mirrorHit ).display();
  return json;
}

Json::Value VirtualCubeWindowResult::provenanceJson() const
{
  Json::Value array( Json::arrayValue );
  for ( const VirtualCubeProvenance &entry : provenance )
    array.append( entry.toJson() );
  return array;
}

VirtualCubeSourceWindow virtualCubeSourceWindow( const RasterMetadata &metadata, double minX,
                                                 double minY, double maxX, double maxY )
{
  VirtualCubeSourceWindow result;
  if ( metadata.width <= 0 || metadata.height <= 0 || !metadata.hasGeotransform )
    return result;
  const std::array<double, 6> &gt = metadata.geotransform;
  if ( gt[1] == 0.0 || gt[5] == 0.0 )
    return result;
  const double srcX0 = ( minX - gt[0] ) / gt[1];
  const double srcX1 = ( maxX - gt[0] ) / gt[1];
  const double srcY0 = ( maxY - gt[3] ) / gt[5];
  const double srcY1 = ( minY - gt[3] ) / gt[5];
  int sx0 = static_cast<int>( std::floor( std::min( srcX0, srcX1 ) ) );
  int sy0 = static_cast<int>( std::floor( std::min( srcY0, srcY1 ) ) );
  int sx1 = static_cast<int>( std::ceil( std::max( srcX0, srcX1 ) ) );
  int sy1 = static_cast<int>( std::ceil( std::max( srcY0, srcY1 ) ) );
  sx0 = std::max( sx0, 0 );
  sy0 = std::max( sy0, 0 );
  sx1 = std::min( sx1, metadata.width );
  sy1 = std::min( sy1, metadata.height );
  if ( sx1 <= sx0 || sy1 <= sy0 )
    return result;
  result.ok = true;
  result.window = RasterWindow { sx0, sy0, sx1 - sx0, sy1 - sy0 };
  return result;
}

// --- build -------------------------------------------------------------------

VirtualCube VirtualCube::build( const std::vector<AssetRecord> &assets, const VirtualCubeGrid &grid,
                                OverlapPolicy overlap, const VirtualCubeQuality &quality,
                                const VirtualCubeBuildOptions &options, const CancelToken &cancel )
{
  if ( assets.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "virtual cube needs at least one asset" );
  if ( grid.explicitGrid && !grid.valid() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "explicit cube grid is not valid (CRS, scales, extent)" );

  VirtualCube cube;
  cube.mOverlap = overlap;

  // Selection order is the overlap priority (D-1006/D-1007).
  const std::vector<std::size_t> order = selectionOrder( assets, quality );
  cube.mAssets.reserve( assets.size() );
  for ( const std::size_t index : order )
  {
    VirtualCubeAssetIndexEntry entry;
    entry.record = assets[index];
    const InstantParse parse = parseIso8601Instant( assets[index].datetimeUtc );
    entry.instantUtc = parse.ok ? instantToUtcString( parse.epochNanos ) : std::string();
    cube.mAssets.push_back( std::move( entry ) );
  }

  if ( grid.explicitGrid )
  {
    cube.mGrid = grid;
    cube.mGrid.scaleX = positiveScale( grid.scaleX );
    cube.mGrid.scaleY = positiveScale( grid.scaleY );
    return cube;   // no probe: the caller declared the grid, IO stays lazy
  }

  // Grid negotiation over a BOUNDED metadata probe (declared cost, hard cap).
  int probed = 0;
  VirtualCubeAssetIndexEntry *winner = nullptr;
  AssetNativeGrid winnerGrid;
  for ( VirtualCubeAssetIndexEntry &entry : cube.mAssets )
  {
    if ( probed >= options.probeLimit )
      break;
    if ( cancel.cancelled() )
      throw GeoError( ErrorCode::Cancelled, "virtual cube build cancelled during grid probe" );
    ++probed;
    entry.probed = true;
    try
    {
      const RasterReader reader = RasterReader::open( fabricCachedPath( entry.record.path ) );
      const AssetNativeGrid native = nativeGridOf( reader.metadata() );
      entry.readable = true;
      entry.rasterWidth = reader.metadata().width;
      entry.rasterHeight = reader.metadata().height;
      if ( native.ok )
      {
        entry.hasGrid = true;
        entry.resX = native.resX;
        entry.resY = native.resY;
        entry.assetMinX = native.minX;
        entry.assetMinY = native.minY;
        entry.assetMaxX = native.maxX;
        entry.assetMaxY = native.maxY;
        entry.epsgAuthid = native.authid;
        // Highest resolution wins; ties break by (authid, scale sum, id) —
        // all declared facts, no network (D-1007 family).
        const bool better = winner == nullptr || native.resX < winnerGrid.resX ||
                            ( native.resX == winnerGrid.resX && native.resY < winnerGrid.resY ) ||
                            ( native.resX == winnerGrid.resX && native.resY == winnerGrid.resY &&
                              ( native.authid < winnerGrid.authid ||
                                ( native.authid == winnerGrid.authid &&
                                  entry.record.id < winner->record.id ) ) );
        if ( better )
        {
          winner = &entry;
          winnerGrid = native;
        }
      }
    }
    catch ( const GeoError &error )
    {
      entry.readable = false;
      entry.probeError = error.what();
    }
  }

  if ( winner == nullptr )
    throw GeoError( ErrorCode::Unsupported,
                    "cube grid could not be derived: no probed asset carries resolution facts "
                    "(declare an explicit grid)" );
  cube.mGrid.explicitGrid = false;
  cube.mGrid.crs.valid = !winnerGrid.authid.empty();
  cube.mGrid.crs.authid = winnerGrid.authid;
  cube.mGrid.scaleX = winnerGrid.resX;
  cube.mGrid.scaleY = winnerGrid.resY;
  // Coverage = union of PROBED asset extents (declared facts only).
  double minX = 0, minY = 0, maxX = 0, maxY = 0;
  bool first = true;
  for ( const VirtualCubeAssetIndexEntry &entry : cube.mAssets )
  {
    if ( !entry.hasGrid )
      continue;
    if ( first )
    {
      minX = entry.assetMinX;
      minY = entry.assetMinY;
      maxX = entry.assetMaxX;
      maxY = entry.assetMaxY;
      first = false;
      continue;
    }
    minX = std::min( minX, entry.assetMinX );
    minY = std::min( minY, entry.assetMinY );
    maxX = std::max( maxX, entry.assetMaxX );
    maxY = std::max( maxY, entry.assetMaxY );
  }
  cube.mGrid.minX = minX;
  cube.mGrid.minY = minY;
  cube.mGrid.maxX = maxX;
  cube.mGrid.maxY = maxY;
  return cube;
}

Json::Value VirtualCube::describeJson() const
{
  Json::Value json;
  json["grid"] = mGrid.toJson();
  json["overlap"] = overlapPolicyName( mOverlap );
  json["assetCount"] = static_cast<Json::UInt64>( mAssets.size() );
  Json::Value assetsArray( Json::arrayValue );
  std::uint64_t probed = 0, readable = 0, undated = 0;
  for ( const VirtualCubeAssetIndexEntry &entry : mAssets )
  {
    probed += entry.probed ? 1 : 0;
    readable += entry.readable ? 1 : 0;
    undated += entry.instantUtc.empty() ? 1 : 0;
    if ( static_cast<int>( assetsArray.size() ) < 64 )   // bounded preview
    {
      Json::Value asset;
      asset["id"] = entry.record.id;
      asset["instantUtc"] = entry.instantUtc;
      asset["probed"] = entry.probed;
      asset["readable"] = entry.readable;
      asset["hasGrid"] = entry.hasGrid;
      assetsArray.append( asset );
    }
  }
  json["assetsPreview"] = assetsArray;
  json["probed"] = probed;
  json["readable"] = readable;
  json["undated"] = undated;
  return json;
}

// --- window read --------------------------------------------------------------

VirtualCubeWindowResult VirtualCube::readWindow( int xOff, int yOff, int width, int height,
                                                 const VirtualCubeReadOptions &options,
                                                 const CancelToken &cancel ) const
{
  if ( width <= 0 || height <= 0 )
    throw GeoError( ErrorCode::InvalidArgument, "cube window size must be positive" );
  if ( xOff < 0 || yOff < 0 || xOff + width > mGrid.width() || yOff + height > mGrid.height() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "cube window escapes the grid extent (clamp explicitly if intended)" );

  VirtualCubeWindowResult result;
  result.width = width;
  result.height = height;
  result.values.assign( static_cast<std::size_t>( width ) * height, 0.0 );
  std::vector<std::uint8_t> filled( static_cast<std::size_t>( width ) * height, 0 );
  // Mirror lookups need identity tokens; resolve lazily per asset (fail-
  // closed: an unprovable asset simply never mirror-hits).
  std::map<std::string, std::string> mirrorTokens;

  // Target extent of the window (north-up grid, positive scales).
  const double targetMinX = mGrid.minX + xOff * mGrid.scaleX;
  const double targetMaxX = mGrid.minX + ( xOff + width ) * mGrid.scaleX;
  const double targetMinY = mGrid.maxY - ( yOff + height ) * mGrid.scaleY;
  const double targetMaxY = mGrid.maxY - yOff * mGrid.scaleY;

  bool noDataSet = false;
  for ( const VirtualCubeAssetIndexEntry &entry : mAssets )
  {
    if ( cancel.cancelled() )
      throw GeoError( ErrorCode::Cancelled, "cube window read cancelled" );

    VirtualCubeProvenance provenance;
    provenance.assetId = entry.record.id;
    provenance.assetPath = entry.record.path;
    provenance.instantUtc = entry.instantUtc;
    provenance.targetMinX = targetMinX;
    provenance.targetMinY = targetMinY;
    provenance.targetMaxX = targetMaxX;
    provenance.targetMaxY = targetMaxY;

    // Intersection test on the record's declared bbox (index facts).
    if ( !entry.record.hasBbox || entry.record.maxX <= targetMinX ||
         entry.record.minX >= targetMaxX || entry.record.maxY <= targetMinY ||
         entry.record.minY >= targetMaxY )
    {
      // Declared bbox never intersects the window: the asset is NOT
      // consulted (nothing opened, nothing read) and earns no provenance.
      continue;
    }

    try
    {
      RasterReader reader = RasterReader::open( fabricCachedPath( entry.record.path ) );
      const RasterMetadata &metadata = reader.metadata();

      // CRS contract (D-1005): the cube is not a warp engine.
      if ( mGrid.crs.valid && !metadata.crs.authid.empty() &&
           metadata.crs.authid != mGrid.crs.authid )
      {
        provenance.failed = true;
        provenance.errorText = "asset CRS " + metadata.crs.authid + " differs from grid CRS " +
                               mGrid.crs.authid + " (reproject upstream — io:warp/io:reproject)";
        result.provenance.push_back( std::move( provenance ) );
        continue;
      }

      std::string bandHow;
      const int band = resolveBandIndex( metadata, options, bandHow );
      if ( band == 0 )
      {
        provenance.failed = true;
        provenance.errorText = "band not resolvable (role '" + options.bandRole + "', index " +
                               std::to_string( options.bandIndex ) + ")";
        result.provenance.push_back( std::move( provenance ) );
        continue;
      }

      // Source pixel window covering the target extent.
      const std::array<double, 6> &gt = metadata.geotransform;
      if ( metadata.width <= 0 || !metadata.hasGeotransform || gt[1] == 0.0 || gt[5] == 0.0 )
      {
        provenance.failed = true;
        provenance.errorText = "asset carries no usable geotransform";
        result.provenance.push_back( std::move( provenance ) );
        continue;
      }
      const double srcX0 = ( targetMinX - gt[0] ) / gt[1];
      const double srcX1 = ( targetMaxX - gt[0] ) / gt[1];
      const double srcY0 = ( targetMaxY - gt[3] ) / gt[5];
      const double srcY1 = ( targetMinY - gt[3] ) / gt[5];
      int sx0 = static_cast<int>( std::floor( std::min( srcX0, srcX1 ) ) );
      int sy0 = static_cast<int>( std::floor( std::min( srcY0, srcY1 ) ) );
      int sx1 = static_cast<int>( std::ceil( std::max( srcX0, srcX1 ) ) );
      int sy1 = static_cast<int>( std::ceil( std::max( srcY0, srcY1 ) ) );
      sx0 = std::max( sx0, 0 );
      sy0 = std::max( sy0, 0 );
      sx1 = std::min( sx1, metadata.width );
      sy1 = std::min( sy1, metadata.height );
      if ( sx1 <= sx0 || sy1 <= sy0 )
      {
        result.provenance.push_back( std::move( provenance ) );
        continue;
      }
      const RasterWindow sourceWindow { sx0, sy0, sx1 - sx0, sy1 - sy0 };
      provenance.sourceWindow = sourceWindow;

      // Mirror hit (needs the source window for the key).
      std::string mirrorHitPath;
      if ( !options.mirrorDirectory.empty() )
      {
        std::string token = entry.identityToken;
        if ( token.empty() )
        {
          const auto cached = mirrorTokens.find( entry.record.id );
          if ( cached == mirrorTokens.end() )
          {
            // A failed identity probe (offline remote, unreadable file)
            // means NO mirror key — the asset falls through to the normal
            // read path and fails there with its own typed error. Never a
            // guess, never a walk-aborting throw.
            try
            {
              token = assetIdentityToken( entry.record.path, AssetIdentityOptions{} ).token;
            }
            catch ( const GeoError & )
            {
              token.clear();
            }
            mirrorTokens[entry.record.id] = token;
          }
          else
            token = cached->second;
        }
        // Mirrors materialize band 1 (the mirror walk's selector) — a
        // window resolved to any other band must not consume those bytes.
        if ( !token.empty() && band == 1 )
        {
          const std::string key =
            fabricChunkMirrorKey( token, entry.record.path, sourceWindow, "band1" );
          std::string skippedCorrupt;
          const std::string hit =
            resolveMirrorHit( options.mirrorDirectory, token, key, &skippedCorrupt );
          if ( !hit.empty() )
          {
            mirrorHitPath = hit;
            provenance.mirrorHit = hit;
          }
        }
      }

      // No mirror hit: the already-open asset reader is the source. A hit
      // opens the mirrored chunk raster, which holds the chunk window; its
      // own metadata may pad to block boundaries — clamp.
      RasterWindow readWindow = sourceWindow;
      std::vector<double> values;
      const BandInfo *bandInfo = nullptr;
      if ( mirrorHitPath.empty() )
      {
        values = reader.readWindow( { band }, readWindow, options.maxWindowBytes );
        bandInfo = &metadata.bands[static_cast<std::size_t>( band - 1 )];
      }
      else
      {
        RasterReader mirrored = RasterReader::open( mirrorHitPath );
        readWindow = RasterWindow { 0, 0, std::min( sourceWindow.width, mirrored.metadata().width ),
                                    std::min( sourceWindow.height, mirrored.metadata().height ) };
        values = mirrored.readWindow( { 1 }, readWindow, options.maxWindowBytes );
        bandInfo = &mirrored.metadata().bands[0];
        // Provenance reports the bytes ACTUALLY read (the mirrored raster
        // may pad to block boundaries).
        provenance.sourceWindow = readWindow;
      }

      if ( !noDataSet )
      {
        result.gridNoData = bandInfo->hasNoData && !bandInfo->noDataIsNaN ? bandInfo->noDataValue
                                                                          : -9999.0;
        result.noDataIsNaN = bandInfo->hasNoData && bandInfo->noDataIsNaN;
        result.values.assign( static_cast<std::size_t>( width ) * height, result.gridNoData );
        noDataSet = true;
      }

      // FirstWins scatter: every target cell whose source pixel exists and
      // is not yet filled takes this asset's stored value. NN at the cell
      // center — deterministic, documented, no hidden resampling kernel.
      bool contributedAny = false;
      for ( int ty = 0; ty < height; ++ty )
      {
        const double worldY = targetMaxY - ( ty + 0.5 ) * mGrid.scaleY;
        const double fy = ( worldY - gt[3] ) / gt[5];
        const int sy = static_cast<int>( std::floor( fy ) );
        if ( sy < readWindow.yOff || sy >= readWindow.yOff + readWindow.height )
          continue;
        for ( int tx = 0; tx < width; ++tx )
        {
          if ( filled[static_cast<std::size_t>( ty ) * width + tx] )
            continue;
          const double worldX = targetMinX + ( tx + 0.5 ) * mGrid.scaleX;
          const double fx = ( worldX - gt[0] ) / gt[1];
          const int sx = static_cast<int>( std::floor( fx ) );
          if ( sx < readWindow.xOff || sx >= readWindow.xOff + readWindow.width )
            continue;
          const std::size_t sourceIndex =
            static_cast<std::size_t>( sy - readWindow.yOff ) * readWindow.width +
            ( sx - readWindow.xOff );
          const double value = values[sourceIndex];
          // Declared-NoData source pixels do not WIN the FirstWins contest —
          // they leave the hole open for a later asset (mask-aware filling).
          if ( bandInfo->hasNoData && value == bandInfo->noDataValue )
            continue;
          result.values[static_cast<std::size_t>( ty ) * width + tx] = value;
          filled[static_cast<std::size_t>( ty ) * width + tx] = 1;
          contributedAny = true;
        }
      }
      provenance.contributed = contributedAny;
      result.provenance.push_back( std::move( provenance ) );
    }
    catch ( const GeoError &error )
    {
      provenance.failed = true;
      provenance.errorText = error.what();
      result.provenance.push_back( std::move( provenance ) );
    }
  }

  if ( !noDataSet )
  {
    // No asset could even declare a NoData: the window is honestly empty.
    result.gridNoData = -9999.0;
    result.values.assign( static_cast<std::size_t>( width ) * height, result.gridNoData );
  }
  return result;
}

} // namespace sicnu::geo

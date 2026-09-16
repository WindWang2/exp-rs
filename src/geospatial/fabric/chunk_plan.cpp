/***************************************************************************
  geospatial/fabric/chunk_plan.cpp — named-dimension chunk plans.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/fabric/chunk_plan.h"

#include "geospatial/fabric/object_store.h"
#include "geospatial/raster/raster_reader.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::geo
{

namespace
{

/// ceil division with overflow-refusing count product.
std::int64_t chunkCount( std::int64_t size, std::int64_t chunk )
{
  return ( size + chunk - 1 ) / chunk;
}

std::uint64_t multiplyCounts( const std::vector<CubeChunkDim> &dims )
{
  std::uint64_t total = 1;
  for ( const CubeChunkDim &dim : dims )
  {
    if ( dim.count <= 0 )
      return 0;
    const std::uint64_t count = static_cast<std::uint64_t>( dim.count );
    if ( total > std::numeric_limits<std::uint64_t>::max() / count )
      throw GeoError( ErrorCode::ResourceExhausted,
                      "chunk count overflows u64 — the slicing must be narrowed" );
    total *= count;
  }
  return total;
}

double bytesPerCellOf( const std::string &dtype )
{
  // Declared dtype sizes (canonical vocabulary); unknown → 0 (bytesUnknown).
  if ( dtype == "Byte" || dtype == "Int8" )
    return 1.0;
  if ( dtype == "UInt16" || dtype == "Int16" )
    return 2.0;
  if ( dtype == "UInt32" || dtype == "Int32" || dtype == "Float32" || dtype == "CInt16" )
    return 4.0;
  if ( dtype == "Float64" || dtype == "CFloat32" || dtype == "CInt32" )
    return 8.0;
  if ( dtype == "CFloat64" )
    return 16.0;
  return 0.0;
}

} // namespace

Json::Value CubeChunkShape::toJson() const
{
  Json::Value json;
  json["time"] = time;
  json["y"] = y;
  json["x"] = x;
  json["band"] = band;
  return json;
}

void CubeSlice::validate() const
{
  if ( !timeStartUtc.empty() )
  {
    if ( !parseIso8601Instant( timeStartUtc ).ok )
      throw GeoError( ErrorCode::InvalidArgument, "slice timeStartUtc is not an ISO-8601 instant" );
  }
  if ( !timeEndUtc.empty() )
  {
    if ( !parseIso8601Instant( timeEndUtc ).ok )
      throw GeoError( ErrorCode::InvalidArgument, "slice timeEndUtc is not an ISO-8601 instant" );
  }
  if ( hasSpatialSlice && ( minX >= maxX || minY >= maxY ) )
    throw GeoError( ErrorCode::InvalidArgument, "slice spatial bounds cross" );
  // 11.0: explicit dimension ranges are half-open and shape-checked here
  // (bounds against a concrete axis are checked at plan time).
  for ( const auto &entry : dimensionRanges )
  {
    const auto &[ begin, end ] = entry.second;
    if ( entry.first.empty() )
      throw GeoError( ErrorCode::InvalidArgument, "slice dimension range names an empty axis" );
    if ( begin < 0 || begin >= end )
      throw GeoError( ErrorCode::InvalidArgument,
                      "slice dimension range must satisfy 0 <= begin < end: " + entry.first );
  }
}

Json::Value CubeChunkRequest::toJson() const
{
  Json::Value json;
  json["index"] = static_cast<Json::UInt64>( index );
  Json::Value coords( Json::arrayValue );
  for ( const std::int64_t c : chunkCoords )
    coords.append( static_cast<Json::Int64>( c ) );
  json["chunkCoords"] = coords;
  Json::Value offsets( Json::arrayValue );
  for ( const std::int64_t o : dimOffsets )
    offsets.append( static_cast<Json::Int64>( o ) );
  json["dimOffsets"] = offsets;
  Json::Value sizes( Json::arrayValue );
  for ( const std::int64_t sz : dimSizes )
    sizes.append( static_cast<Json::Int64>( sz ) );
  json["dimSizes"] = sizes;
  if ( !timeUtc.empty() )
    json["timeUtc"] = timeUtc;
  if ( hasExtent )
  {
    Json::Value extent( Json::arrayValue );
    extent.append( minX );
    extent.append( minY );
    extent.append( maxX );
    extent.append( maxY );
    json["extent"] = extent;
  }
  json["estimatedBytes"] = static_cast<Json::UInt64>( estimatedBytes );
  if ( !assetIdHint.empty() )
    json["assetIdHint"] = assetIdHint;
  return json;
}

Json::Value CubeChunkDim::toJson() const
{
  Json::Value json;
  json["name"] = name;
  json["size"] = static_cast<Json::Int64>( size );
  json["chunk"] = static_cast<Json::Int64>( chunk );
  json["count"] = static_cast<Json::Int64>( count );
  return json;
}

CubeChunkPlan CubeChunkPlan::forVirtualCube( const VirtualCube &cube, const CubeChunkShape &shape,
                                             const CubeSlice &slice )
{
  slice.validate();
  const VirtualCubeGrid &grid = cube.grid();
  if ( !grid.valid() )
    throw GeoError( ErrorCode::InvalidArgument, "cube grid is invalid — cannot chunk" );

  // Time dim: the cube's selection order (one asset per step). Slicing by
  // UTC instant narrows BEFORE counting (D-1008).
  std::vector<std::string> instants;
  std::vector<std::string> assetIdByTime;
  std::vector<std::int64_t> timeIndices;   // selected asset slots
  std::int64_t timeOffset = 0;
  for ( std::size_t i = 0; i < cube.assets().size(); ++i )
  {
    const VirtualCubeAssetIndexEntry &entry = cube.assets()[i];
    if ( !slice.timeStartUtc.empty() || !slice.timeEndUtc.empty() )
    {
      if ( entry.instantUtc.empty() )
        continue;   // undated assets drop under a temporal slice (no evidence)
      const InstantParse parse = parseIso8601Instant( entry.instantUtc );
      if ( !parse.ok )
        continue;
      if ( !slice.timeStartUtc.empty() )
      {
        const InstantParse start = parseIso8601Instant( slice.timeStartUtc );
        if ( start.ok && parse.epochNanos < start.epochNanos )
          continue;
      }
      if ( !slice.timeEndUtc.empty() )
      {
        const InstantParse end = parseIso8601Instant( slice.timeEndUtc );
        if ( end.ok && parse.epochNanos >= end.epochNanos )
          continue;   // end exclusive
      }
    }
    timeIndices.push_back( static_cast<std::int64_t>( i ) );
    instants.push_back( entry.instantUtc );
    assetIdByTime.push_back( entry.record.id );
  }
  // mInstants is already the post-slice list — the EO chunk's time base
  // starts at 0 within it (the cube slot offset is NOT added: chunk coords
  // index the sliced plan, not the raw asset list).
  timeOffset = 0;

  // Spatial slice → grid pixel bounds.
  std::int64_t y0 = 0, x0 = 0;
  std::int64_t ySize = grid.height();
  std::int64_t xSize = grid.width();
  if ( slice.hasSpatialSlice )
  {
    const double width = grid.width();
    const double height = grid.height();
    const double px0 = std::floor( ( slice.minX - grid.minX ) / grid.scaleX );
    const double px1 = std::ceil( ( slice.maxX - grid.minX ) / grid.scaleX );
    const double py0 = std::floor( ( grid.maxY - slice.maxY ) / grid.scaleY );
    const double py1 = std::ceil( ( grid.maxY - slice.minY ) / grid.scaleY );
    x0 = static_cast<std::int64_t>( std::max( 0.0, px0 ) );
    y0 = static_cast<std::int64_t>( std::max( 0.0, py0 ) );
    xSize = static_cast<std::int64_t>( std::clamp( px1 - x0, 0.0, width - static_cast<double>( x0 ) ) );
    ySize = static_cast<std::int64_t>( std::clamp( py1 - y0, 0.0, height - static_cast<double>( y0 ) ) );
  }

  // Band dim: role/index selection narrows; the cube exposes one band per
  // read (bandIndex/bandRole), so the plan's band dim is the selection SIZE.
  std::int64_t bandSize = 1;
  if ( !slice.bandRoles.empty() || !slice.bandIndices.empty() )
  {
    const std::size_t wanted =
      slice.bandRoles.empty() ? slice.bandIndices.size()
                              : std::max( slice.bandRoles.size(), slice.bandIndices.size() );
    bandSize = static_cast<std::int64_t>( std::max<std::size_t>( wanted, 1 ) );
  }

  const std::int64_t timeChunk = shape.time > 0 ? shape.time : 1;
  const std::int64_t yChunk = shape.y > 0 ? shape.y : 256;
  const std::int64_t xChunk = shape.x > 0 ? shape.x : 256;
  const std::int64_t bandChunk = shape.band > 0 ? shape.band : 1;

  CubeChunkPlan plan;
  plan.mIsEo = true;
  plan.mGrid = grid;
  // Byte facts: one bounded metadata open of the FIRST selectable asset
  // (declared cost, same doctrine as the grid probe) — unknown stays 0.
  if ( !cube.assets().empty() )
  {
    try
    {
      const RasterReader reader =
        RasterReader::open( fabricCachedPath( cube.assets().front().record.path ) );
      if ( !reader.metadata().bands.empty() )
        plan.mBytesPerCell = bytesPerCellOf( reader.metadata().bands.front().dtype );
    }
    catch ( const GeoError & )
    {
      plan.mBytesPerCell = 0.0;   // bytesUnknown stays honest
    }
  }
  plan.mInstants = instants;
  plan.mAssetIdByTime = assetIdByTime;
  plan.mTimeSliced = !slice.timeStartUtc.empty() || !slice.timeEndUtc.empty();
  plan.mSpatialSliced = slice.hasSpatialSlice;
  plan.mBandSliced = !slice.bandRoles.empty() || !slice.bandIndices.empty();

  const auto addDim = [ & ]( const std::string &name, std::int64_t size, std::int64_t chunk ) {
    CubeChunkDim dim;
    dim.name = name;
    dim.size = size;
    dim.chunk = chunk;
    dim.count = size > 0 ? chunkCount( size, chunk ) : 0;
    plan.mDims.push_back( dim );
  };
  addDim( "time", static_cast<std::int64_t>( instants.size() ), timeChunk );
  addDim( "y", ySize, yChunk );
  addDim( "x", xSize, xChunk );
  addDim( "band", bandSize, bandChunk );
  plan.mChunkCountTotal = multiplyCounts( plan.mDims );
  return plan;
}

CubeChunkPlan CubeChunkPlan::forMultidimDescriptor( const MultidimCubeDescriptor &descriptor,
                                                    const CubeChunkShape &shape,
                                                    const CubeSlice &slice )
{
  slice.validate();
  if ( !slice.bandRoles.empty() || !slice.bandIndices.empty() )
    throw GeoError( ErrorCode::Unsupported,
                    "multidim stores carry no band roles — use CubeSlice::dimensionRanges "
                    "with the axis name (e.g. \"band\") instead" );

  CubeChunkPlan plan;
  plan.mIsEo = false;
  plan.mMultidimDescriptor = descriptor;

  // Map the descriptor's dimensions: time = TEMPORAL type or named "time"
  // (at most one); the two trailing NON-TIME dims map to y/x; everything
  // else keeps its name. A trailing time axis cannot satisfy the y/x
  // mapping — that is a typed refusal, never a silently-zero plan (11.0).
  const std::size_t dimCount = descriptor.dimensionNames.size();
  std::size_t timeIndex = dimCount;
  for ( std::size_t i = 0; i < dimCount; ++i )
  {
    const MultidimCubeAxis &axis = descriptor.axes[i];
    if ( axis.type == "TEMPORAL" || axis.name == "time" )
    {
      timeIndex = i;
      break;
    }
  }
  if ( dimCount >= 1 && timeIndex != dimCount && timeIndex + 2 >= dimCount )
    throw GeoError( ErrorCode::Unsupported,
                    "multidim descriptor maps no y/x pair: the time axis is trailing "
                    "(descriptor dims must end with two spatial dimensions)" );
  if ( dimCount < 3 )
    throw GeoError( ErrorCode::Unsupported,
                    "multidim descriptor needs at least 3 dimensions (…, y, x)" );

  std::int64_t timeSize = 0;
  std::int64_t timeChunk = shape.time > 0 ? shape.time : 1;
  std::int64_t ySize = 0, xSize = 0;
  std::int64_t yChunk = shape.y > 0 ? shape.y : 256;
  std::int64_t xChunk = shape.x > 0 ? shape.x : 256;
  // 11.0 per-name chunk shapes: middle dims honor shape.perDimension[name];
  // the historical whole-extent fallback stays shape.x (10.0 behavior).
  const auto chunkFor = [ &shape ]( const std::string &name ) {
    const auto it = shape.perDimension.find( name );
    if ( it != shape.perDimension.end() )
    {
      if ( it->second <= 0 )
        throw GeoError( ErrorCode::InvalidArgument,
                        "per-dimension chunk shape must be positive: " + name );
      return it->second;
    }
    return ( shape.x > 0 ? shape.x : 256 );
  };

  const std::size_t yIndex = dimCount - 2;
  const std::size_t xIndex = dimCount - 1;
  ySize = descriptor.axes[yIndex].size;
  xSize = descriptor.axes[xIndex].size;

  // --- Time slicing (11.0): instant-window over the descriptor's resolved
  // instants. A bounded (truncated) axis capture cannot prove membership —
  // typed refusal, absence is not evidence. Unresolvable instants refuse.
  std::vector<std::int64_t> selectedTime;
  const bool wantsTimeSlice = !slice.timeStartUtc.empty() || !slice.timeEndUtc.empty();
  if ( timeIndex != dimCount )
  {
    const MultidimCubeAxis &axis = descriptor.axes[timeIndex];
    timeSize = axis.size;
    if ( wantsTimeSlice )
    {
      if ( !axis.instantsResolved || axis.instantsUtc.empty() )
        throw GeoError( ErrorCode::Unsupported,
                        "time slice needs resolved axis instants: " + axis.name );
      if ( axis.valuesBounded || axis.stringValuesBounded ||
           axis.instantsUtc.size() < static_cast<std::size_t>( axis.size ) )
        throw GeoError( ErrorCode::Unsupported,
                        "time axis capture is bounded — cannot slice reliably: " + axis.name );
      const std::int64_t startNanos =
        slice.timeStartUtc.empty()
          ? std::numeric_limits<std::int64_t>::min()
          : parseIso8601Instant( slice.timeStartUtc ).epochNanos;
      const std::int64_t endNanos =
        slice.timeEndUtc.empty()
          ? std::numeric_limits<std::int64_t>::max()
          : parseIso8601Instant( slice.timeEndUtc ).epochNanos;
      if ( ( !slice.timeStartUtc.empty() && !parseIso8601Instant( slice.timeStartUtc ).ok ) ||
           ( !slice.timeEndUtc.empty() && !parseIso8601Instant( slice.timeEndUtc ).ok ) )
        throw GeoError( ErrorCode::InvalidArgument, "unparseable time slice bound" );
      for ( std::size_t i = 0; i < axis.instantsUtc.size(); ++i )
      {
        const InstantParse instant = parseIso8601Instant( axis.instantsUtc[i] );
        if ( !instant.ok )
          continue;   // an unresolvable label cannot prove membership
        if ( instant.epochNanos >= startNanos && instant.epochNanos < endNanos )
          selectedTime.push_back( static_cast<std::int64_t>( i ) );
      }
      if ( static_cast<std::int64_t>( selectedTime.size() ) != timeSize )
      {
        plan.mTimeSliced = true;
        timeSize = static_cast<std::int64_t>( selectedTime.size() );
        plan.mMultidimSelection["time"] = selectedTime;
      }
    }
  }

  // --- Spatial slicing (11.0): bbox → pixel window through the descriptor's
  // geotransform (same sign-safe mapping as the cube read path).
  std::int64_t yOffset = 0, xOffset = 0;
  if ( slice.hasSpatialSlice )
  {
    if ( !descriptor.hasGeoTransform || descriptor.geotransform.size() != 6 )
      throw GeoError( ErrorCode::Unsupported,
                      "spatial slice needs a declared geotransform on the descriptor" );
    const std::vector<double> &gt = descriptor.geotransform;
    const double srcX0 = ( slice.minX - gt[0] ) / gt[1];
    const double srcX1 = ( slice.maxX - gt[0] ) / gt[1];
    const double srcY0 = ( slice.maxY - gt[3] ) / gt[5];
    const double srcY1 = ( slice.minY - gt[3] ) / gt[5];
    std::int64_t sx0 = static_cast<std::int64_t>( std::floor( std::min( srcX0, srcX1 ) ) );
    std::int64_t sy0 = static_cast<std::int64_t>( std::floor( std::min( srcY0, srcY1 ) ) );
    std::int64_t sx1 = static_cast<std::int64_t>( std::ceil( std::max( srcX0, srcX1 ) ) );
    std::int64_t sy1 = static_cast<std::int64_t>( std::ceil( std::max( srcY0, srcY1 ) ) );
    sx0 = std::max<std::int64_t>( sx0, 0 );
    sy0 = std::max<std::int64_t>( sy0, 0 );
    sx1 = std::min<std::int64_t>( sx1, xSize );
    sy1 = std::min<std::int64_t>( sy1, ySize );
    if ( sx1 <= sx0 || sy1 <= sy0 )
      throw GeoError( ErrorCode::InvalidArgument,
                      "spatial slice misses the descriptor's raster extent" );
    xOffset = sx0;
    yOffset = sy0;
    xSize = sx1 - sx0;
    ySize = sy1 - sy0;
    plan.mSpatialSliced = true;
  }

  const auto addDim = [ & ]( const std::string &name, std::int64_t size, std::int64_t chunk ) {
    CubeChunkDim dim;
    dim.name = name;
    dim.size = size;
    dim.chunk = chunk;
    dim.count = size > 0 ? chunkCount( size, chunk ) : 0;
    plan.mDims.push_back( dim );
  };

  // Middle dims in declaration order (skipping time): apply explicit
  // dimensionRanges (bounds-checked against the axis) or keep whole.
  for ( std::size_t i = 0; i < dimCount; ++i )
  {
    if ( i == timeIndex || i == yIndex || i == xIndex )
      continue;
    const MultidimCubeAxis &axis = descriptor.axes[i];
    std::int64_t size = axis.size;
    const auto range = slice.dimensionRanges.find( axis.name );
    if ( range != slice.dimensionRanges.end() )
    {
      const auto [ begin, end ] = range->second;
      if ( begin < 0 || end > axis.size || begin >= end )
        throw GeoError( ErrorCode::InvalidArgument,
                        "dimension range [" + std::to_string( begin ) + "," +
                          std::to_string( end ) + ") out of bounds for axis " + axis.name );
      size = end - begin;
      plan.mBandSliced = true;   // "a slice narrowed a non-spatial dim"
      std::vector<std::int64_t> selection;
      selection.reserve( static_cast<std::size_t>( size ) );
      for ( std::int64_t k = begin; k < end; ++k )
        selection.push_back( k );
      plan.mMultidimSelection[axis.name] = selection;
    }
    addDim( axis.name, size, chunkFor( axis.name ) );
  }

  addDim( "time", timeSize, timeChunk );
  addDim( "y", ySize, yChunk );
  addDim( "x", xSize, xChunk );
  plan.mBytesPerCell = bytesPerCellOf( descriptor.dtype );
  plan.mChunkCountTotal = multiplyCounts( plan.mDims );
  return plan;
}

std::vector<CubeChunkRequest> CubeChunkPlan::materializeChunks( std::uint64_t begin,
                                                                std::size_t maxCount ) const
{
  if ( begin >= mChunkCountTotal )
    throw GeoError( ErrorCode::InvalidArgument,
                    "chunk index " + std::to_string( begin ) + " beyond total " +
                      std::to_string( mChunkCountTotal ) );
  std::vector<CubeChunkRequest> chunks;
  const std::uint64_t last = std::min( mChunkCountTotal, begin + maxCount );
  chunks.reserve( static_cast<std::size_t>( std::min<std::uint64_t>( last - begin, 4096 ) ) );
  for ( std::uint64_t index = begin; index < last; ++index )
  {
    // Mixed-radix decomposition over dims() — the fixed total order.
    std::vector<std::int64_t> chunkCoords( mDims.size(), 0 );
    std::vector<std::int64_t> dimOffsets( mDims.size(), 0 );
    std::vector<std::int64_t> dimSizes( mDims.size(), 0 );
    std::uint64_t remainder = index;
    for ( std::size_t d = mDims.size(); d-- > 0; )
    {
      const std::uint64_t count = static_cast<std::uint64_t>( mDims[d].count );
      const std::uint64_t coord = count > 0 ? remainder % count : 0;
      remainder = count > 0 ? remainder / count : 0;
      chunkCoords[d] = static_cast<std::int64_t>( coord );
      dimOffsets[d] = coord * mDims[d].chunk;
      dimSizes[d] = std::min<std::int64_t>( mDims[d].chunk,
                                            mDims[d].size - dimOffsets[d] );
    }

    CubeChunkRequest request;
    request.index = index;
    request.chunkCoords = chunkCoords;
    request.dimOffsets = dimOffsets;
    request.dimSizes = dimSizes;

    if ( mIsEo )
    {
      // EO facts: time instant + grid extent of the chunk.
      if ( !mDims.empty() && mDims[0].name == "time" && !mInstants.empty() )
      {
        const std::int64_t timeIndex = dimOffsets[0];   // mInstants is post-slice
        if ( timeIndex >= 0 &&
             timeIndex < static_cast<std::int64_t>( mInstants.size() ) )
        {
          request.timeUtc = mInstants[static_cast<std::size_t>( timeIndex )];
          if ( static_cast<std::size_t>( timeIndex ) < mAssetIdByTime.size() )
            request.assetIdHint = mAssetIdByTime[static_cast<std::size_t>( timeIndex )];
        }
      }
      // y/x extent (dims 1 and 2 for EO plans).
      if ( mDims.size() >= 3 && mGrid.valid() )
      {
        const std::int64_t yBegin = dimOffsets[1];
        const std::int64_t xBegin = dimOffsets[2];
        const double minX = mGrid.minX + static_cast<double>( xBegin ) * mGrid.scaleX;
        const double maxX = mGrid.minX + static_cast<double>( xBegin + dimSizes[2] ) * mGrid.scaleX;
        const double maxY = mGrid.maxY - static_cast<double>( yBegin ) * mGrid.scaleY;
        const double minY = mGrid.maxY - static_cast<double>( yBegin + dimSizes[1] ) * mGrid.scaleY;
        request.hasExtent = true;
        request.minX = minX;
        request.minY = minY;
        request.maxX = maxX;
        request.maxY = maxY;
      }
    }

    // Estimated bytes from declared dtype facts (0 stays honest).
    double cells = 1.0;
    for ( const std::int64_t size : dimSizes )
      cells *= static_cast<double>( size );
    request.estimatedBytes = mBytesPerCell > 0.0
                               ? static_cast<std::uint64_t>( cells * mBytesPerCell )
                               : 0;
    chunks.push_back( std::move( request ) );
  }
  return chunks;
}

Json::Value CubeChunkPlan::toJson() const
{
  Json::Value json;
  Json::Value dims( Json::arrayValue );
  for ( const CubeChunkDim &dim : mDims )
    dims.append( dim.toJson() );
  json["dims"] = dims;
  json["chunkCountTotal"] = static_cast<Json::UInt64>( mChunkCountTotal );
  json["timeSliced"] = mTimeSliced;
  json["spatialSliced"] = mSpatialSliced;
  json["bandSliced"] = mBandSliced;
  json["kind"] = mIsEo ? "eo_cube" : "multidim";
  return json;
}

} // namespace sicnu::geo

/***************************************************************************
  geospatial/multidim/multidim_view.cpp
  Geospatial I/O Foundation 4.0 — lazy multidimensional access contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/multidim/multidim_view.h"

#include "geospatial/gdal_guard.h"

#include <cpl_conv.h>
#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace sicnu::geo
{
namespace
{

GDALDatasetH datasetOf( void *handle ) { return static_cast<GDALDatasetH>( handle ); }

struct ArrayLease
{
    GDALMDArrayH array = nullptr;
    ~ArrayLease()
    {
      if ( array )
        GDALMDArrayRelease( array );
    }
};

GDALDataType requireRealDataType( GDALExtendedDataTypeH dataType )
{
  if ( GDALExtendedDataTypeGetClass( dataType ) != GEDTC_NUMERIC )
    throw GeoError( ErrorCode::Unsupported, "Variable has a non-numeric type; slicing is undefined" );
  const GDALDataType type = GDALExtendedDataTypeGetNumericDataType( dataType );
  switch ( type )
  {
    case GDT_Byte:
    case GDT_UInt16:
    case GDT_Int16:
    case GDT_UInt32:
    case GDT_Int32:
    case GDT_Float32:
    case GDT_Float64:
      return type;
    default:
      throw GeoError( ErrorCode::Unsupported, "Variable pixel type has no exact double representation" );
  }
}

} // namespace

Json::Value MultidimGrid::toJson() const
{
  Json::Value json;
  json["rows"] = static_cast<Json::UInt64>( rows );
  json["cols"] = static_cast<Json::UInt64>( cols );
  json["has_nodata"] = hasNoData;
  if ( hasNoData )
  {
    if ( noDataIsNaN )
      json["nodata"] = "nan";
    else
      json["nodata"] = noDataValue;
  }
  return json;
}

MultidimView MultidimView::open( const std::string &path )
{
  if ( path.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "MultidimView::open: empty path" );

  ensureGdalRegistered();
  QuietCplErrors quiet;
  GDALDatasetH handle = GDALOpenEx( path.c_str(), GDAL_OF_READONLY | GDAL_OF_MULTIDIM_RASTER, nullptr, nullptr, nullptr );
  if ( !handle )
    throw GeoError( ErrorCode::OpenFailed, "Cannot open multidimensional store: " + path );

  MultidimView view;
  view.mHandle = handle;
  try
  {
    view.mMetadata = inspectMultidim( path );
  }
  catch ( ... )
  {
    GDALClose( datasetOf( view.mHandle ) );
    view.mHandle = nullptr;
    throw;
  }
  return view;
}

MultidimView::~MultidimView() { close(); }

MultidimView::MultidimView( MultidimView &&other ) noexcept
  : mHandle( std::exchange( other.mHandle, nullptr ) )
  , mMetadata( std::move( other.mMetadata ) )
{
}

MultidimView &MultidimView::operator=( MultidimView &&other ) noexcept
{
  if ( this != &other )
  {
    close();
    mHandle = std::exchange( other.mHandle, nullptr );
    mMetadata = std::move( other.mMetadata );
  }
  return *this;
}

void MultidimView::close()
{
  if ( mHandle )
    GDALClose( datasetOf( mHandle ) );
  mHandle = nullptr;
}

MultidimGrid MultidimView::readSlice( const std::string &variable,
                                      const std::vector<std::pair<std::string, std::int64_t>> &dimSlices,
                                      std::size_t maxCells )
{
  if ( !mHandle )
    throw GeoError( ErrorCode::InvalidArgument, "readSlice: view is closed" );

  GDALGroupH rootGroup = GDALDatasetGetRootGroup( datasetOf( mHandle ) );
  if ( !rootGroup )
    throw GeoError( ErrorCode::Unsupported, "Store exposes no multidimensional API" );
  ArrayLease lease;
  lease.array = GDALGroupOpenMDArray( rootGroup, variable.c_str(), nullptr );
  GDALGroupRelease( rootGroup );
  if ( !lease.array )
    throw GeoError( ErrorCode::InvalidArgument, "Variable not found: " + variable );

  // Resolve dimension layout against the declared metadata.
  const VariableInfo *info = nullptr;
  for ( const VariableInfo &candidate : mMetadata.variables )
  {
    if ( candidate.name == variable )
    {
      info = &candidate;
      break;
    }
  }
  if ( !info )
    throw GeoError( ErrorCode::InvalidArgument, "Variable not described in metadata: " + variable );

  const std::size_t dimCount = info->dimensionNames.size();
  if ( dimCount < 2 )
    throw GeoError( ErrorCode::Unsupported, "Variable has fewer than two dimensions; no spatial grid exists" );

  // The two free dims are the LAST two (fastest-varying spatial convention);
  // every other dim must be sliced exactly once, unknown names are errors.
  std::map<std::string, std::int64_t> slices;
  for ( const auto &slice : dimSlices )
    slices[slice.first] = slice.second;

  std::vector<GUInt64> start( dimCount, 0 );
  std::vector<std::size_t> count( dimCount, 1 );
  const std::string &rowDim = info->dimensionNames[dimCount - 2];
  const std::string &colDim = info->dimensionNames[dimCount - 1];
  std::int64_t rows = 0;
  std::int64_t cols = 0;
  for ( const DimensionInfo &dim : mMetadata.dimensions )
  {
    const bool isRow = dim.name == rowDim;
    const bool isCol = dim.name == colDim;
    const auto sliced = slices.find( dim.name );
    if ( isRow )
    {
      if ( sliced != slices.end() )
        throw GeoError( ErrorCode::InvalidArgument, "The row dimension '" + dim.name + "' must remain free" );
      rows = dim.size;
    }
    else if ( isCol )
    {
      if ( sliced != slices.end() )
        throw GeoError( ErrorCode::InvalidArgument, "The column dimension '" + dim.name + "' must remain free" );
      cols = dim.size;
    }
    else
    {
      if ( sliced == slices.end() )
        throw GeoError( ErrorCode::InvalidArgument,
                        "Dimension '" + dim.name + "' is not sliced; flatten-to-bands is forbidden by contract" );
      const std::int64_t index = sliced->second;
      if ( index < 0 || index >= dim.size )
      {
        Json::Value details;
        details["dimension"] = dim.name;
        details["index"] = index;
        details["size"] = dim.size;
        throw GeoError( ErrorCode::InvalidArgument, "Slice index out of range", details );
      }
      start[&dim - mMetadata.dimensions.data()] = static_cast<GUInt64>( index );
    }
  }
  for ( std::size_t i = 0; i < dimCount; ++i )
  {
    if ( i == dimCount - 2 )
      count[i] = static_cast<std::size_t>( rows );
    else if ( i == dimCount - 1 )
      count[i] = static_cast<std::size_t>( cols );
  }

  const std::size_t cells = static_cast<std::size_t>( rows ) * static_cast<std::size_t>( cols );
  if ( cells > maxCells )
  {
    Json::Value details;
    details["cells"] = static_cast<Json::UInt64>( cells );
    details["budget"] = static_cast<Json::UInt64>( maxCells );
    throw GeoError( ErrorCode::Unsupported, "Slice exceeds the declared cell budget", details );
  }

  GDALExtendedDataTypeH dataType = GDALMDArrayGetDataType( lease.array );
  const GDALDataType type = requireRealDataType( dataType );
  GDALExtendedDataTypeRelease( dataType );

  std::vector<GInt64> step( dimCount, 1 );
  std::vector<GPtrDiff_t> stride( dimCount, 1 );
  // Contiguous row-major scatter for the two free dims.
  stride[dimCount - 1] = 1;
  stride[dimCount - 2] = static_cast<GPtrDiff_t>( cols );

  MultidimGrid grid;
  grid.rows = static_cast<std::size_t>( rows );
  grid.cols = static_cast<std::size_t>( cols );
  grid.values.assign( cells, 0.0 );

  size_t allocSize = cells * sizeof( double );
  QuietCplErrors quiet;
  GDALExtendedDataTypeH bufferType = GDALExtendedDataTypeCreate( GDT_Float64 );
  const bool readOk = GDALMDArrayRead( lease.array, start.data(), count.data(), step.data(), stride.data(),
                                       bufferType, grid.values.data(), grid.values.data(), allocSize ) != 0;
  GDALExtendedDataTypeRelease( bufferType );
  if ( !readOk )
    throw GeoError( ErrorCode::IoError, "readSlice: array read failed for " + variable );

  grid.hasNoData = info->hasNoData;
  grid.noDataValue = info->noDataValue;
  grid.noDataIsNaN = info->noDataIsNaN;
  return grid;
}

MultidimGrid MultidimView::readTemporalOrLevelSlice( const std::string &variable,
                                                     const std::string &dimensionName,
                                                     std::int64_t index, std::size_t maxCells )
{
  // The named dimension is the one SLICED at `index`; all remaining
  // dimensions (including both spatial dims) stay free for readSlice.
  std::vector<std::pair<std::string, std::int64_t>> slices;
  bool namedFound = false;
  for ( const VariableInfo &candidate : mMetadata.variables )
  {
    if ( candidate.name != variable )
      continue;
    for ( const std::string &dim : candidate.dimensionNames )
    {
      if ( dim == dimensionName )
      {
        slices.emplace_back( dim, index );
        namedFound = true;
      }
    }
    break;
  }
  if ( !namedFound )
    throw GeoError( ErrorCode::InvalidArgument, "Variable or sliced dimension not found: " + variable );
  return readSlice( variable, slices, maxCells );
}

} // namespace sicnu::geo

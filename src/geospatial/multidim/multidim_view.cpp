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
#include <limits>
#include <cstring>
#include <set>
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
  json["missing_count"] = static_cast<Json::UInt64>( missingCount );
  return json;
}

const char *coordinateMatchName( CoordinateMatch match )
{
  switch ( match )
  {
    case CoordinateMatch::Exact: return "exact";
    case CoordinateMatch::Nearest: return "nearest";
  }
  return "exact";
}

CoordinateMatch coordinateMatchFromName( const std::string &name )
{
  if ( name == "exact" ) return CoordinateMatch::Exact;
  if ( name == "nearest" ) return CoordinateMatch::Nearest;
  Json::Value details;
  details["name"] = name;
  throw GeoError( ErrorCode::InvalidArgument, "coordinateMatchFromName: unknown match mode", details );
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
  return readSliceWindow( variable, dimSlices, 0, 0, 0, 0, maxCells );
}

MultidimGrid MultidimView::readSliceWindow( const std::string &variable,
                                            const std::vector<std::pair<std::string, std::int64_t>> &dimSlices,
                                            std::int64_t rowOff, std::int64_t colOff,
                                            std::size_t windowRows, std::size_t windowCols,
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
  std::int64_t rows = 0;
  std::int64_t cols = 0;

  // Look up each of the VARIABLE's dimensions by name in the metadata —
  // a multi-variable store carries root dimensions this variable does not
  // use, and iterating the root list would misindex start/count (out of
  // bounds) or reject unused dimensions spuriously.
  std::map<std::string, std::int64_t> dimensionSizes;
  for ( const DimensionInfo &dim : mMetadata.dimensions )
    dimensionSizes[dim.name] = dim.size;

  std::set<std::string> consumed;
  for ( std::size_t d = 0; d < dimCount; ++d )
  {
    const std::string &name = info->dimensionNames[d];
    const bool isRow = d == dimCount - 2;
    const bool isCol = d == dimCount - 1;
    const auto sliced = slices.find( name );
    const std::int64_t size = dimensionSizes.count( name ) ? dimensionSizes[name] : 0;
    if ( isRow )
    {
      if ( sliced != slices.end() )
        throw GeoError( ErrorCode::InvalidArgument, "The row dimension '" + name + "' must remain free" );
      // A declared window restricts the free extent; it must lie inside the
      // dimension — a window is a bound, never a clamp-and-lie. 0 means
      // "the whole dimension".
      rows = windowRows > 0 ? static_cast<std::int64_t>( windowRows ) : size;
      // Overflow-safe bound check: rowOff can be adversarially large.
      if ( rowOff < 0 || rows > size || rowOff > size - rows )
      {
        Json::Value details;
        details["dimension"] = name;
        details["offset"] = rowOff;
        details["window_rows"] = static_cast<Json::UInt64>( windowRows );
        details["size"] = size;
        throw GeoError( ErrorCode::InvalidArgument, "Row window outside the dimension extent", details );
      }
      start[d] = static_cast<GUInt64>( rowOff );
      count[d] = static_cast<std::size_t>( rows );
    }
    else if ( isCol )
    {
      if ( sliced != slices.end() )
        throw GeoError( ErrorCode::InvalidArgument, "The column dimension '" + name + "' must remain free" );
      cols = windowCols > 0 ? static_cast<std::int64_t>( windowCols ) : size;
      if ( colOff < 0 || cols > size || colOff > size - cols )
      {
        Json::Value details;
        details["dimension"] = name;
        details["offset"] = colOff;
        details["window_cols"] = static_cast<Json::UInt64>( windowCols );
        details["size"] = size;
        throw GeoError( ErrorCode::InvalidArgument, "Column window outside the dimension extent", details );
      }
      start[d] = static_cast<GUInt64>( colOff );
      count[d] = static_cast<std::size_t>( cols );
    }
    else
    {
      if ( sliced == slices.end() )
        throw GeoError( ErrorCode::InvalidArgument,
                        "Dimension '" + name + "' is not sliced; flatten-to-bands is forbidden by contract" );
      const std::int64_t index = sliced->second;
      if ( index < 0 || index >= size )
      {
        Json::Value details;
        details["dimension"] = name;
        details["index"] = index;
        details["size"] = size;
        throw GeoError( ErrorCode::InvalidArgument, "Slice index out of range", details );
      }
      start[d] = static_cast<GUInt64>( index );
      consumed.insert( name );
    }
  }
  // Every requested slice must match one of the variable's dimensions — a
  // slice naming an unknown dimension is a structured error, not a no-op.
  for ( const auto &slice : slices )
  {
    if ( consumed.count( slice.first ) == 0 )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Variable '" + variable + "' has no dimension named '" + slice.first + "'" );
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

  // Missing-value accounting: declared NoData cells (or NaN when NoData is
  // NaN) are counted — values themselves stay untouched (stored values).
  if ( grid.hasNoData )
  {
    std::size_t missing = 0;
    for ( const double value : grid.values )
    {
      const bool isMissing = grid.noDataIsNaN ? std::isnan( value ) : value == grid.noDataValue;
      if ( isMissing )
        ++missing;
    }
    grid.missingCount = missing;
  }
  else
  {
    std::size_t missing = 0;
    for ( const double value : grid.values )
      if ( std::isnan( value ) )
        ++missing;
    grid.missingCount = missing;
  }
  return grid;
}

CoordinateSliceMatch MultidimView::resolveCoordinateIndex( const std::string &dimensionName, double value,
                                                           CoordinateMatch matchMode, double tolerance ) const
{
  const DimensionInfo *axis = nullptr;
  for ( const DimensionInfo &dim : mMetadata.dimensions )
  {
    if ( dim.name == dimensionName )
    {
      axis = &dim;
      break;
    }
  }
  if ( axis == nullptr )
    throw GeoError( ErrorCode::InvalidArgument, "Dimension not found: " + dimensionName );
  if ( !axis->hasValues )
    throw GeoError( ErrorCode::Unsupported,
                    "Dimension '" + dimensionName + "' carries no captured coordinate axis" );

  CoordinateSliceMatch best;
  best.exact = false;
  double bestDistance = std::numeric_limits<double>::infinity();
  for ( std::size_t i = 0; i < axis->values.size(); ++i )
  {
    const double distance = std::fabs( axis->values[i] - value );
    if ( distance == 0.0 )
    {
      best.index = static_cast<std::int64_t>( i );
      best.resolvedValue = axis->values[i];
      best.distance = 0.0;
      best.exact = true;
      return best;
    }
    if ( distance < bestDistance )
    {
      bestDistance = distance;
      best.index = static_cast<std::int64_t>( i );
      best.resolvedValue = axis->values[i];
      best.distance = distance;
    }
  }
  // Exact mode never falls back to a neighbour; Nearest must stay within
  // the declared tolerance — otherwise the answer is a typed miss.
  if ( matchMode == CoordinateMatch::Exact )
    throw GeoError( ErrorCode::NotFound,
                    "Coordinate value not present on axis '" + dimensionName + "'" );
  if ( bestDistance > tolerance )
  {
    Json::Value details;
    details["dimension"] = dimensionName;
    details["requested"] = value;
    details["nearest"] = best.resolvedValue;
    details["distance"] = bestDistance;
    details["tolerance"] = tolerance;
    throw GeoError( ErrorCode::NotFound, "Nearest coordinate beyond the declared tolerance", details );
  }
  return best;
}

MultidimGrid MultidimView::readSliceByCoordinateValues(
  const std::string &variable,
  const std::vector<std::pair<std::string, double>> &dimValues,
  CoordinateMatch matchMode, double tolerance, std::size_t maxCells )
{
  std::vector<std::pair<std::string, std::int64_t>> dimSlices;
  dimSlices.reserve( dimValues.size() );
  for ( const auto &entry : dimValues )
  {
    const CoordinateSliceMatch match = resolveCoordinateIndex( entry.first, entry.second, matchMode, tolerance );
    dimSlices.emplace_back( entry.first, match.index );
  }
  return readSlice( variable, dimSlices, maxCells );
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

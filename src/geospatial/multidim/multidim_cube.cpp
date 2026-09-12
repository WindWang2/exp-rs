/***************************************************************************
  geospatial/multidim/multidim_cube.cpp — logical EO cube descriptor (M5).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/multidim/multidim_cube.h"

#include "geospatial/multidim/multidim_view.h"
#include "geospatial/util/time_normalization.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace sicnu::geo
{

namespace
{

const DimensionInfo *dimensionOf( const MultidimMetadata &metadata, const std::string &name )
{
  for ( const DimensionInfo &dimension : metadata.dimensions )
  {
    if ( dimension.name == name )
      return &dimension;
  }
  return nullptr;
}

/// CF-relative units (CF §4.4): "<unit> since <instant>" — e.g.
/// "hours since 2026-01-01 00:00:00" — where the unit is one of
/// second(s)|minute(s)|hour(s)|day(s) (a leading numeric count is tolerated
/// but the axis VALUES carry the counts; the unit defines the scale).
/// Returns the per-unit seconds multiplier and the epoch instant's nanos.
/// Anything else fails — an unparsable unit resolves NO instants (verbatim
/// values stay the truth).
bool cfUnitEpoch( const std::string &unit, double &multiplierOut, std::int64_t &epochNanosOut )
{
  const std::size_t since = unit.rfind( " since " );
  if ( since == std::string::npos )
    return false;
  std::string unitName = unit.substr( 0, since );
  const std::string epochText = unit.substr( since + 7 );
  if ( unitName.empty() || epochText.empty() )
    return false;

  // Lowercase; strip a leading numeric count ("12 hours") if present.
  for ( char &c : unitName )
    c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
  std::size_t nameStart = 0;
  while ( nameStart < unitName.size()
          && ( std::isdigit( static_cast<unsigned char>( unitName[nameStart] ) )
               || unitName[nameStart] == ' ' ) )
    ++nameStart;
  std::string name = unitName.substr( nameStart );
  while ( !name.empty() && name.front() == ' ' )
    name.erase( name.begin() );
  while ( !name.empty() && name.back() == ' ' )
    name.pop_back();
  if ( name.size() > 1 && name.back() == 's' )
    name.pop_back();

  if ( name == "second" )
    multiplierOut = 1.0;
  else if ( name == "minute" )
    multiplierOut = 60.0;
  else if ( name == "hour" )
    multiplierOut = 3600.0;
  else if ( name == "day" )
    multiplierOut = 86400.0;
  else
    return false;

  const InstantParse epoch = parseIso8601Instant( epochText );
  if ( !epoch.ok )
    return false;
  epochNanosOut = epoch.epochNanos;
  return true;
}

/// Instants for one axis: string datetime labels, or numeric CF-relative
/// values. All-or-nothing: a single unresolvable entry resolves NOTHING
/// (never a partially normalized axis). maxNanos bounds the int64 range.
std::vector<std::string> instantsForAxis( const DimensionInfo &dimension, bool &resolved )
{
  static constexpr double kMaxSafeNanos = 9.2e18; // int64 max ~9.22e18
  resolved = false;

  if ( dimension.hasStringValues )
  {
    std::vector<std::string> instants;
    instants.reserve( dimension.stringValues.size() );
    for ( const std::string &label : dimension.stringValues )
    {
      const InstantParse parsed = parseIso8601Instant( label );
      if ( !parsed.ok )
        return std::vector<std::string>();
      instants.push_back( instantToUtcString( parsed.epochNanos ) );
    }
    if ( instants.empty() )
      return std::vector<std::string>();
    resolved = true;
    return instants;
  }

  if ( dimension.hasValues && !dimension.values.empty() && !dimension.unit.empty() )
  {
    double multiplier = 0.0;
    std::int64_t epochNanos = 0;
    if ( !cfUnitEpoch( dimension.unit, multiplier, epochNanos ) )
      return std::vector<std::string>();
    std::vector<std::string> instants;
    instants.reserve( dimension.values.size() );
    for ( const double value : dimension.values )
    {
      if ( !std::isfinite( value ) )
        return std::vector<std::string>();
      const double offsetNanos = value * multiplier * 1e9;
      if ( std::fabs( offsetNanos ) > kMaxSafeNanos )
        return std::vector<std::string>();
      const double total = static_cast<double>( epochNanos ) + offsetNanos;
      if ( std::fabs( total ) > kMaxSafeNanos )
        return std::vector<std::string>();
      instants.push_back( instantToUtcString( static_cast<std::int64_t>( total ) ) );
    }
    resolved = true;
    return instants;
  }
  return std::vector<std::string>();
}

Json::Value doubleArrayJson( const std::vector<double> &values )
{
  Json::Value array( Json::arrayValue );
  for ( const double value : values )
    array.append( value );
  return array;
}

Json::Value int64ArrayJson( const std::vector<std::int64_t> &values )
{
  Json::Value array( Json::arrayValue );
  for ( const std::int64_t value : values )
    array.append( static_cast<Json::Int64>( value ) );
  return array;
}

} // namespace

// ---------------------------------------------------------------------------
// MultidimCubeAxis
// ---------------------------------------------------------------------------

Json::Value MultidimCubeAxis::toJson() const
{
  Json::Value json;
  json["name"] = name;
  json["type"] = type;
  json["unit"] = unit;
  json["size"] = static_cast<Json::Int64>( size );
  json["has_numeric_values"] = hasNumericValues;
  json["values_bounded"] = valuesBounded;
  if ( hasNumericValues )
    json["numeric_values"] = doubleArrayJson( numericValues );
  json["has_string_labels"] = hasStringLabels;
  json["string_values_bounded"] = stringValuesBounded;
  if ( hasStringLabels )
  {
    Json::Value labels( Json::arrayValue );
    for ( const std::string &label : stringLabels )
      labels.append( label );
    json["string_labels"] = labels;
  }
  json["instants_resolved"] = instantsResolved;
  if ( instantsResolved )
  {
    Json::Value instants( Json::arrayValue );
    for ( const std::string &instant : instantsUtc )
      instants.append( instant );
    json["instants_utc"] = instants;
  }
  return json;
}

MultidimCubeAxis MultidimCubeAxis::fromJson( const Json::Value &json )
{
  MultidimCubeAxis axis;
  axis.name = json["name"].asString();
  axis.type = json["type"].asString();
  axis.unit = json["unit"].asString();
  axis.size = json["size"].asInt64();
  axis.hasNumericValues = json["has_numeric_values"].asBool();
  axis.valuesBounded = json["values_bounded"].asBool();
  if ( axis.hasNumericValues )
  {
    for ( const Json::Value &value : json["numeric_values"] )
      axis.numericValues.push_back( value.asDouble() );
  }
  axis.hasStringLabels = json["has_string_labels"].asBool();
  axis.stringValuesBounded = json["string_values_bounded"].asBool();
  if ( axis.hasStringLabels )
  {
    for ( const Json::Value &label : json["string_labels"] )
      axis.stringLabels.push_back( label.asString() );
  }
  axis.instantsResolved = json["instants_resolved"].asBool();
  if ( axis.instantsResolved )
  {
    for ( const Json::Value &instant : json["instants_utc"] )
      axis.instantsUtc.push_back( instant.asString() );
  }
  return axis;
}

// ---------------------------------------------------------------------------
// MultidimCubeDescriptor
// ---------------------------------------------------------------------------

Json::Value MultidimCubeDescriptor::toJson() const
{
  Json::Value json;
  json["path"] = path;
  json["driver"] = driver;
  json["variable"] = variable;
  json["dtype"] = dtype;
  json["unit"] = unit;
  json["has_no_data"] = hasNoData;
  if ( hasNoData )
  {
    if ( noDataIsNaN )
      json["no_data"] = "nan";
    else
      json["no_data"] = noDataValue;
  }
  json["has_scale"] = hasScale;
  if ( hasScale )
    json["scale"] = scale;
  json["has_offset"] = hasOffset;
  if ( hasOffset )
    json["offset"] = offset;
  json["band_role"] = bandRole;
  Json::Value dims( Json::arrayValue );
  for ( const std::string &name : dimensionNames )
    dims.append( name );
  json["dimension_names"] = dims;
  Json::Value axisJson( Json::arrayValue );
  for ( const MultidimCubeAxis &axis : axes )
    axisJson.append( axis.toJson() );
  json["axes"] = axisJson;
  json["crs"] = crs.toJson();
  json["has_geotransform"] = hasGeoTransform;
  if ( hasGeoTransform )
    json["geotransform"] = doubleArrayJson( geotransform );
  if ( !blockShape.empty() )
    json["block_shape"] = int64ArrayJson( blockShape );
  return json;
}

MultidimCubeDescriptor MultidimCubeDescriptor::fromJson( const Json::Value &json )
{
  if ( !json.isObject() )
    throw GeoError( ErrorCode::InvalidMetadata, "cube descriptor is not an object" );
  MultidimCubeDescriptor cube;
  cube.path = json["path"].asString();
  cube.driver = json["driver"].asString();
  cube.variable = json["variable"].asString();
  cube.dtype = json["dtype"].asString();
  cube.unit = json["unit"].asString();
  cube.hasNoData = json["has_no_data"].asBool();
  if ( cube.hasNoData )
  {
    const Json::Value &noData = json["no_data"];
    if ( noData.isString() && noData.asString() == "nan" )
      cube.noDataIsNaN = true;
    else
      cube.noDataValue = noData.asDouble();
  }
  cube.hasScale = json["has_scale"].asBool();
  if ( cube.hasScale )
    cube.scale = json["scale"].asDouble();
  cube.hasOffset = json["has_offset"].asBool();
  if ( cube.hasOffset )
    cube.offset = json["offset"].asDouble();
  cube.bandRole = json["band_role"].asString();
  for ( const Json::Value &name : json["dimension_names"] )
    cube.dimensionNames.push_back( name.asString() );
  if ( cube.dimensionNames.empty() )
    throw GeoError( ErrorCode::InvalidMetadata, "cube descriptor carries no dimensions" );
  if ( cube.variable.empty() )
    throw GeoError( ErrorCode::InvalidMetadata, "cube descriptor carries no variable" );
  for ( const Json::Value &axisJson : json["axes"] )
    cube.axes.push_back( MultidimCubeAxis::fromJson( axisJson ) );
  if ( cube.axes.size() != cube.dimensionNames.size() )
    throw GeoError( ErrorCode::InvalidMetadata, "cube descriptor axes do not match dimensions" );
  for ( std::size_t i = 0; i < cube.axes.size(); ++i )
  {
    if ( cube.axes[i].name != cube.dimensionNames[i] )
      throw GeoError( ErrorCode::InvalidMetadata, "cube descriptor axis order does not match dimensions" );
  }
  cube.crs = CrsInfo::fromJson( json["crs"] );
  cube.hasGeoTransform = json["has_geotransform"].asBool();
  if ( cube.hasGeoTransform )
  {
    for ( const Json::Value &value : json["geotransform"] )
      cube.geotransform.push_back( value.asDouble() );
    if ( cube.geotransform.size() != 6 )
      throw GeoError( ErrorCode::InvalidMetadata, "cube geotransform must carry 6 values" );
  }
  for ( const Json::Value &value : json["block_shape"] )
    cube.blockShape.push_back( value.asInt64() );
  return cube;
}

MultidimCubeDescriptor describeCube( const MultidimView &view, const std::string &variable )
{
  const MultidimMetadata &metadata = view.metadata();
  const VariableInfo *variableInfo = nullptr;
  for ( const VariableInfo &candidate : metadata.variables )
  {
    if ( candidate.name == variable )
    {
      variableInfo = &candidate;
      break;
    }
  }
  if ( variableInfo == nullptr )
  {
    Json::Value details;
    details["variable"] = variable;
    throw GeoError( ErrorCode::InvalidArgument, "describeCube: unknown variable", details );
  }

  MultidimCubeDescriptor cube;
  cube.path = metadata.path;
  cube.driver = metadata.driver;
  cube.variable = variableInfo->name;
  cube.dtype = variableInfo->dtype;
  cube.unit = variableInfo->unit;
  cube.hasNoData = variableInfo->hasNoData;
  cube.noDataValue = variableInfo->noDataValue;
  cube.noDataIsNaN = variableInfo->noDataIsNaN;
  cube.hasScale = variableInfo->hasScale;
  cube.scale = variableInfo->scale;
  cube.hasOffset = variableInfo->hasOffset;
  cube.offset = variableInfo->offset;
  // Role: the variable's declared role attribute ("role" or the canonical
  // SICNU_BAND_ROLE key); "" when undeclared — never guessed from the name.
  for ( const char *roleKey : { "role", "SICNU_BAND_ROLE" } )
  {
    const auto it = variableInfo->attributes.find( roleKey );
    if ( it != variableInfo->attributes.end() && !it->second.empty() )
    {
      cube.bandRole = it->second;
      break;
    }
  }
  cube.dimensionNames = variableInfo->dimensionNames;
  cube.blockShape = variableInfo->blockShape;
  cube.crs = metadata.crs;

  cube.axes.reserve( cube.dimensionNames.size() );
  for ( const std::string &dimensionName : cube.dimensionNames )
  {
    MultidimCubeAxis axis;
    const DimensionInfo *dimension = dimensionOf( metadata, dimensionName );
    if ( dimension == nullptr )
    {
      // A variable dimension without a matching store dimension (broken
      // store): the descriptor still carries the name, size 0, no capture.
      axis.name = dimensionName;
      axis.size = 0;
      cube.axes.push_back( std::move( axis ) );
      continue;
    }
    axis.name = dimension->name;
    axis.type = dimension->type;
    axis.unit = dimension->unit;
    axis.size = dimension->size;
    axis.hasNumericValues = dimension->hasValues;
    axis.valuesBounded = dimension->valuesBounded;
    axis.numericValues = dimension->values;
    axis.hasStringLabels = dimension->hasStringValues;
    axis.stringValuesBounded = dimension->stringValuesBounded;
    axis.stringLabels = dimension->stringValues;
    axis.instantsUtc = instantsForAxis( *dimension, axis.instantsResolved );
    cube.axes.push_back( std::move( axis ) );
  }
  // Spatial anchor: the store's declared geotransform (the descriptor
  // reports what the store declares — it never synthesizes grid geometry).
  double transform[6] = { 0, 0, 0, 0, 0, 0 };
  if ( view.geotransform( transform ) )
  {
    cube.hasGeoTransform = true;
    cube.geotransform.assign( transform, transform + 6 );
  }
  return cube;
}

} // namespace sicnu::geo

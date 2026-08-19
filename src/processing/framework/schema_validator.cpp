// src/processing/framework/schema_validator.cpp
#include "schema_validator.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sicnu::processing {

namespace {

/// Compact JSON value → string for error "actual" fields.
std::string jsonValueToString( const Json::Value &v )
{
  if ( v.isNull() ) return "null";
  if ( v.isBool() ) return v.asBool() ? "true" : "false";
  if ( v.isNumeric() ) return v.asString();
  if ( v.isString() ) return "\"" + v.asString() + "\"";
  if ( v.isArray() ) return "array";
  if ( v.isObject() ) return "object";
  return "?";
}

bool isIntegralValue( const Json::Value &v )
{
  return v.isInt64() || v.isUInt64()
         || ( v.isDouble() && std::floor( v.asDouble() ) == v.asDouble() );
}

bool parseNumericString( const Json::Value &v, double *out )
{
  if ( !v.isString() ) return false;
  const std::string s = v.asString();
  if ( s.empty() ) return false;
  std::size_t pos = 0;
  try
  {
    const double d = std::stod( s, &pos );
    if ( pos != s.size() ) return false;
    if ( out ) *out = d;
    return true;
  }
  catch ( ... )
  {
    return false;
  }
}

bool parseIntegerString( const Json::Value &v, long long *out )
{
  if ( !v.isString() ) return false;
  const std::string s = v.asString();
  if ( s.empty() ) return false;
  std::size_t pos = 0;
  try
  {
    const long long i = std::stoll( s, &pos );
    if ( pos != s.size() ) return false;
    if ( out ) *out = i;
    return true;
  }
  catch ( ... )
  {
    return false;
  }
}

/// A raster/vector/table file port accepts a path string or an object carrying
/// a path-ish member ("path"/"source"/"uri").
bool matchesFileShape( const Json::Value &v )
{
  if ( v.isString() && !v.asString().empty() ) return true;
  if ( v.isObject() )
  {
    for ( const char *key : { "path", "source", "uri" } )
      if ( v.isMember( key ) && v[key].isString() && !v[key].asString().empty() )
        return true;
  }
  return false;
}

void addIssue( std::vector<ParameterIssue> &issues, const std::string &code,
               const std::string &parameter, const std::string &expected,
               const Json::Value &actual, const std::string &message )
{
  ParameterIssue issue;
  issue.code = code;
  issue.parameter = parameter;
  issue.expected = expected;
  issue.actual = jsonValueToString( actual );
  issue.message = message;
  issues.push_back( std::move( issue ) );
}

/// Validates a single value against a port. Returns true when valid;
/// otherwise appends an error and returns false.
bool validateValue( const PortDescriptor &port, const Json::Value &value,
                    std::vector<ParameterIssue> &errors, const std::string &path )
{
  if ( port.isArray )
  {
    if ( !value.isArray() )
    {
      addIssue( errors, "type_mismatch", path, "array", value,
                "Expected an array for parameter '" + path + "'." );
      return false;
    }
    std::vector<ParameterIssue> itemErrors;
    for ( Json::ArrayIndex i = 0; i < value.size(); ++i )
    {
      const std::string itemPath = path + "[" + std::to_string( i ) + "]";
      const Json::Value &item = value[i];
      bool itemOk = true;
      switch ( port.itemType )
      {
        case DataType::Integer:
          itemOk = item.isNumeric() ? isIntegralValue( item ) : parseIntegerString( item, nullptr );
          break;
        case DataType::Numeric:
          itemOk = item.isNumeric() || parseNumericString( item, nullptr );
          break;
        case DataType::Boolean:
          itemOk = item.isBool();
          break;
        case DataType::Enum:
        {
          if ( item.isString() )
            itemOk = std::find( port.enumOptions.begin(), port.enumOptions.end(),
                                item.asString() ) != port.enumOptions.end();
          else if ( item.isNumeric() && isIntegralValue( item ) )
            itemOk = item.asInt64() >= 0 && static_cast<std::size_t>( item.asInt64() ) < port.enumOptions.size();
          else
            itemOk = false;
          break;
        }
        case DataType::String:
        case DataType::Any:
        default:
          itemOk = item.isString() || item.isNumeric() || item.isBool();
          break;
      }
      if ( !itemOk )
      {
        addIssue( itemErrors, "array_item_type", itemPath, dataTypeToString( port.itemType ),
                  item, "Array item at '" + itemPath + "' has an invalid type." );
      }
    }
    errors.insert( errors.end(), itemErrors.begin(), itemErrors.end() );
    return itemErrors.empty();
  }

  bool valid = true;
  switch ( port.type )
  {
    case DataType::Raster:
    case DataType::Vector:
    case DataType::Table:
      valid = matchesFileShape( value );
      if ( !valid )
      {
        addIssue( errors, "invalid_shape", path,
                  port.type == DataType::Raster ? "raster path" : "dataset path", value,
                  "Parameter '" + path + "' must be a dataset path string (or {path}/{source}/{uri})." );
      }
      break;
    case DataType::Numeric:
    {
      double d = 0.0;
      valid = value.isNumeric() || parseNumericString( value, &d );
      if ( !valid )
      {
        addIssue( errors, "type_mismatch", path, "number", value,
                  "Parameter '" + path + "' must be a number." );
      }
      break;
    }
    case DataType::Integer:
    {
      valid = value.isNumeric() ? isIntegralValue( value ) : parseIntegerString( value, nullptr );
      if ( !valid )
      {
        addIssue( errors, "type_mismatch", path, "integer", value,
                  "Parameter '" + path + "' must be an integer." );
      }
      break;
    }
    case DataType::Boolean:
      valid = value.isBool();
      if ( !valid )
      {
        addIssue( errors, "type_mismatch", path, "boolean", value,
                  "Parameter '" + path + "' must be a boolean." );
      }
      break;
    case DataType::Enum:
    {
      if ( value.isString() )
      {
        valid = std::find( port.enumOptions.begin(), port.enumOptions.end(),
                           value.asString() ) != port.enumOptions.end();
      }
      else if ( value.isNumeric() && isIntegralValue( value ) )
      {
        const long long idx = value.asInt64();
        valid = idx >= 0 && static_cast<std::size_t>( idx ) < port.enumOptions.size();
      }
      else
      {
        valid = false;
      }
      if ( !valid )
      {
        std::string expected = "one of: ";
        for ( std::size_t i = 0; i < port.enumOptions.size(); ++i )
        {
          if ( i > 0 ) expected += ", ";
          expected += port.enumOptions[i];
        }
        // Also document numeric-index form for clients that send integer enums.
        expected += " (or integer 0.." + std::to_string( port.enumOptions.size() ? port.enumOptions.size() - 1 : 0 ) + ")";
        addIssue( errors, "enum_mismatch", path, expected, value,
                  "Parameter '" + path + "' is not one of the allowed values." );
      }
      break;
    }
    case DataType::BoundingBox:
      valid = value.isArray() && value.size() >= 4;
      if ( !valid )
      {
        addIssue( errors, "type_mismatch", path, "array of 4 numbers", value,
                  "Parameter '" + path + "' must be an extent array [xmin, ymin, xmax, ymax]." );
      }
      break;
    case DataType::Json:
      valid = value.isObject();
      if ( !valid )
      {
        addIssue( errors, "type_mismatch", path, "object", value,
                  "Parameter '" + path + "' must be a JSON object." );
      }
      break;
    case DataType::Crs:
      valid = value.isString() && !value.asString().empty();
      if ( !valid )
      {
        addIssue( errors, "type_mismatch", path, "CRS string", value,
                  "Parameter '" + path + "' must be a CRS string (e.g. EPSG:4326)." );
      }
      break;
    case DataType::String:
    case DataType::Any:
    default:
      valid = true;
      break;
  }
  if ( !valid ) return false;

  // Numeric range constraints.
  double numeric = 0.0;
  bool hasNumeric = value.isNumeric() || parseNumericString( value, &numeric );
  if ( hasNumeric && value.isNumeric() ) numeric = value.asDouble();
  if ( hasNumeric )
  {
    if ( port.hasMinimum && numeric < port.minimum )
    {
      addIssue( errors, "range_violation", path,
                ">= " + std::to_string( port.minimum ), value,
                "Parameter '" + path + "' is below the minimum " + std::to_string( port.minimum ) + "." );
      valid = false;
    }
    if ( port.hasMaximum && numeric > port.maximum )
    {
      addIssue( errors, "range_violation", path,
                "<= " + std::to_string( port.maximum ), value,
                "Parameter '" + path + "' is above the maximum " + std::to_string( port.maximum ) + "." );
      valid = false;
    }
  }

  return valid;
}

} // namespace

Json::Value ParameterIssue::toJson() const
{
  Json::Value root( Json::objectValue );
  root["code"] = code;
  root["parameter"] = parameter;
  if ( !expected.empty() ) root["expected"] = expected;
  if ( !actual.empty() ) root["actual"] = actual;
  root["message"] = message;
  return root;
}

Json::Value ParameterValidationResult::toJson() const
{
  Json::Value root( Json::objectValue );
  root["valid"] = ok();
  Json::Value errs( Json::arrayValue );
  for ( const auto &e : errors ) errs.append( e.toJson() );
  root["errors"] = errs;
  Json::Value warns( Json::arrayValue );
  for ( const auto &w : warnings ) warns.append( w.toJson() );
  root["warnings"] = warns;
  return root;
}

ParameterValidationResult validateParameters( const Json::Value &params,
                                              const AlgorithmDescriptor &desc,
                                              UnknownParameterPolicy unknown )
{
  ParameterValidationResult result;
  if ( !params.isObject() )
  {
    addIssue( result.errors, "invalid_parameters", "", "object", params,
              "Parameters must be a JSON object." );
    return result;
  }

  // Required presence.
  for ( const auto &port : desc.inputs )
  {
    if ( port.required && !params.isMember( port.name ) )
    {
      addIssue( result.errors, "missing_required", port.name, "present", Json::nullValue,
                "Missing required parameter: " + port.name );
    }
  }

  // Per-parameter validation.
  for ( const auto &member : params.getMemberNames() )
  {
    const PortDescriptor *port = nullptr;
    for ( const auto &p : desc.inputs )
    {
      if ( p.name == member )
      {
        port = &p;
        break;
      }
    }
    if ( !port )
    {
      const std::string message = "Unknown parameter: " + member;
      if ( unknown == UnknownParameterPolicy::Error )
        addIssue( result.errors, "unknown_parameter", member, "declared parameter",
                  params[member], message );
      else if ( unknown == UnknownParameterPolicy::Warn )
        addIssue( result.warnings, "unknown_parameter", member, "declared parameter",
                  params[member], message );
      continue;
    }
    validateValue( *port, params[member], result.errors, member );
  }

  return result;
}

} // namespace sicnu::processing

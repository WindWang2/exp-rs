#include "explain/adapters/registry_operator_knowledge.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"

#include <algorithm>

namespace sicnu::explain::adapters
{
namespace
{

bool isNumeric( const Json::Value &value )
{
  return value.isNumeric() && !value.isString();
}

} // namespace

RegistryOperatorKnowledge::RegistryOperatorKnowledge( sicnu::operators::RSOperatorRegistry &registry )
  : registry_( registry )
{
}

void RegistryOperatorKnowledge::recordDrift( const std::string &operatorId, const char *code,
                                             const std::string &field,
                                             const std::string &message ) const
{
  if ( driftLog_.size() >= kMaxDriftRecords )
  {
    ++droppedDriftRecords_;
    return;
  }
  driftLog_.push_back( AdapterDriftRecord{ operatorId, code, field, message } );
}

std::optional<OperatorFacts> RegistryOperatorKnowledge::findOperator(
  const std::string &operatorId ) const
{
  if ( operatorId.empty() )
    return std::nullopt;

  std::unique_ptr<sicnu::operators::RSOperator> op = registry_.create( operatorId );
  if ( !op )
    return std::nullopt;

  OperatorFacts facts;
  facts.id = op->name();
  facts.displayName = op->displayName();
  facts.group = op->group();
  facts.description = op->description();
  // purpose/useCases/prerequisites/limitations/workflowHints/tags have no
  // authoritative source on the operator surface; they stay empty here and
  // the builder fills authored narrative from the guidance store instead.

  if ( facts.id != operatorId )
  {
    recordDrift( operatorId, "schema_name_mismatch", "operator.name",
                 "registry key '" + operatorId + "' produced instance name '" + facts.id + "'" );
  }

  const Json::Value schema = op->schema();
  if ( !schema.isObject() || !schema["properties"].isObject() )
  {
    recordDrift( facts.id, "schema_properties_not_object", "schema.properties",
                 "schema surface does not carry an object-valued properties member; "
                 "no parameter facts projected" );
    return facts;
  }

  // Root-required names (makeRequired) OR per-parameter "required" flags.
  std::vector<std::string> rootRequired;
  if ( schema["required"].isArray() )
  {
    for ( const Json::Value &name : schema["required"] )
    {
      if ( name.isString() )
        rootRequired.push_back( name.asString() );
    }
  }

  const Json::Value &properties = schema["properties"];
  for ( auto it = properties.begin(); it != properties.end(); ++it )
  {
    const std::string paramName = it.key().asString();
    if ( !it->isObject() )
    {
      recordDrift( facts.id, "schema_param_invalid", "schema.properties." + paramName,
                   "parameter entry is not an object; skipped" );
      continue;
    }
    const Json::Value &prop = *it;

    ParamFact param;
    param.name = paramName;
    if ( prop.isMember( "name" ) && prop["name"].isString() && prop["name"].asString() != paramName )
    {
      recordDrift( facts.id, "schema_name_mismatch", "schema.properties." + paramName + ".name",
                   "embedded name '" + prop["name"].asString() + "' differs from the property key; "
                   "the property key wins" );
    }

    if ( !prop.isMember( "type" ) || !prop["type"].isString() )
    {
      recordDrift( facts.id, "schema_type_missing", "schema.properties." + paramName + ".type",
                   "parameter type is missing or not a string; projected as unknown" );
    }
    else
    {
      param.type = prop["type"].asString();
    }

    if ( prop.isMember( "description" ) && prop["description"].isString() )
      param.description = prop["description"].asString();

    if ( prop.isMember( "default" ) && !prop["default"].isNull() )
      param.defaultValue = prop["default"];

    param.required = std::any_of( rootRequired.begin(), rootRequired.end(),
                                  [ &paramName ]( const std::string &n ) { return n == paramName; } );
    if ( !param.required && prop.isMember( "required" ) && prop["required"].isBool() )
      param.required = prop["required"].asBool();

    if ( prop.isMember( "enum" ) )
    {
      if ( prop["enum"].isArray() )
      {
        for ( const Json::Value &value : prop["enum"] )
        {
          if ( value.isString() )
          {
            param.enumValues.push_back( value.asString() );
          }
          else
          {
            recordDrift( facts.id, "schema_enum_invalid", "schema.properties." + paramName + ".enum",
                         "enum contains a non-string entry; entry skipped" );
          }
        }
      }
      else
      {
        recordDrift( facts.id, "schema_enum_invalid", "schema.properties." + paramName + ".enum",
                     "enum is not an array; no enum values projected" );
      }
    }

    if ( prop.isMember( "minimum" ) )
    {
      if ( isNumeric( prop["minimum"] ) )
        param.minimum = prop["minimum"].asDouble();
      else
        recordDrift( facts.id, "schema_range_invalid", "schema.properties." + paramName + ".minimum",
                     "minimum is not numeric; ignored" );
    }
    if ( prop.isMember( "maximum" ) )
    {
      if ( isNumeric( prop["maximum"] ) )
        param.maximum = prop["maximum"].asDouble();
      else
        recordDrift( facts.id, "schema_range_invalid", "schema.properties." + paramName + ".maximum",
                     "maximum is not numeric; ignored" );
    }

    facts.parameters.push_back( std::move( param ) );
  }

  return facts;
}

} // namespace sicnu::explain::adapters

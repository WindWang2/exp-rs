// src/processing/framework/algorithm_descriptor.cpp
#include "algorithm_descriptor.h"

namespace sicnu::processing {

std::string dataTypeToString( DataType type )
{
  switch ( type )
  {
    case DataType::Raster: return "Raster";
    case DataType::Vector: return "Vector";
    case DataType::Table: return "Table";
    case DataType::Numeric: return "Numeric";
    case DataType::Integer: return "Integer";
    case DataType::String: return "String";
    case DataType::Boolean: return "Boolean";
    case DataType::Enum: return "Enum";
    case DataType::BoundingBox: return "BoundingBox";
    case DataType::Crs: return "Crs";
    case DataType::Json: return "Json";
    case DataType::Any:
    default: return "Any";
  }
}

DataType dataTypeFromString( const std::string &typeStr )
{
  if ( typeStr == "Raster" ) return DataType::Raster;
  if ( typeStr == "Vector" ) return DataType::Vector;
  if ( typeStr == "Table" ) return DataType::Table;
  if ( typeStr == "Numeric" ) return DataType::Numeric;
  if ( typeStr == "Integer" ) return DataType::Integer;
  if ( typeStr == "String" ) return DataType::String;
  if ( typeStr == "Boolean" ) return DataType::Boolean;
  if ( typeStr == "Enum" ) return DataType::Enum;
  if ( typeStr == "BoundingBox" ) return DataType::BoundingBox;
  if ( typeStr == "Crs" ) return DataType::Crs;
  if ( typeStr == "Json" ) return DataType::Json;
  return DataType::Any;
}

Json::Value PortDescriptor::toJsonSchema() const
{
  Json::Value root( Json::objectValue );
  root["description"] = description.empty() ? displayName : description;

  switch ( type )
  {
    case DataType::Numeric:
      root["type"] = "number";
      break;
    case DataType::Integer:
      root["type"] = "integer";
      break;
    case DataType::Boolean:
      root["type"] = "boolean";
      break;
    case DataType::Enum:
      root["type"] = "string";
      if ( !enumOptions.empty() )
      {
        Json::Value arr( Json::arrayValue );
        for ( const auto &opt : enumOptions )
          arr.append( opt );
        root["enum"] = arr;
      }
      break;
    case DataType::Raster:
      root["type"] = "string";
      root["x-ui-type"] = "raster";
      break;
    case DataType::Vector:
      root["type"] = "string";
      root["x-ui-type"] = "vector";
      break;
    case DataType::Table:
      root["type"] = "string";
      root["x-ui-type"] = "table";
      break;
    case DataType::BoundingBox:
      root["type"] = "array";
      root["x-ui-type"] = "bbox";
      break;
    case DataType::Crs:
      root["type"] = "string";
      root["x-ui-type"] = "crs";
      break;
    case DataType::Json:
      root["type"] = "object";
      break;
    case DataType::String:
    case DataType::Any:
    default:
      root["type"] = "string";
      break;
  }

  if ( !defaultValue.empty() )
    root["default"] = defaultValue;

  return root;
}

Json::Value AgentMetadata::toJson() const
{
  Json::Value root( Json::objectValue );
  root["purpose"] = purpose;

  Json::Value tagsArr( Json::arrayValue );
  for ( const auto &t : tags ) tagsArr.append( t );
  root["tags"] = tagsArr;

  Json::Value prereqArr( Json::arrayValue );
  for ( const auto &p : prerequisites ) prereqArr.append( p );
  root["prerequisites"] = prereqArr;

  Json::Value hintsArr( Json::arrayValue );
  for ( const auto &h : workflowHints ) hintsArr.append( h );
  root["workflowHints"] = hintsArr;

  Json::Value limitsArr( Json::arrayValue );
  for ( const auto &l : limitations ) limitsArr.append( l );
  root["limitations"] = limitsArr;

  if ( !llmPromptHint.empty() )
    root["llmPromptHint"] = llmPromptHint;

  if ( !memoryPolicy.empty() )
    root["memoryPolicy"] = memoryPolicy;

  return root;
}

AgentMetadata AgentMetadata::fromJson( const Json::Value &val )
{
  AgentMetadata meta;
  if ( !val.isObject() ) return meta;

  if ( val.isMember( "purpose" ) && val["purpose"].isString() )
    meta.purpose = val["purpose"].asString();

  if ( val.isMember( "tags" ) && val["tags"].isArray() )
  {
    for ( const auto &item : val["tags"] )
      if ( item.isString() ) meta.tags.push_back( item.asString() );
  }

  if ( val.isMember( "prerequisites" ) && val["prerequisites"].isArray() )
  {
    for ( const auto &item : val["prerequisites"] )
      if ( item.isString() ) meta.prerequisites.push_back( item.asString() );
  }

  if ( val.isMember( "workflowHints" ) && val["workflowHints"].isArray() )
  {
    for ( const auto &item : val["workflowHints"] )
      if ( item.isString() ) meta.workflowHints.push_back( item.asString() );
  }

  if ( val.isMember( "limitations" ) && val["limitations"].isArray() )
  {
    for ( const auto &item : val["limitations"] )
      if ( item.isString() ) meta.limitations.push_back( item.asString() );
  }

  if ( val.isMember( "llmPromptHint" ) && val["llmPromptHint"].isString() )
    meta.llmPromptHint = val["llmPromptHint"].asString();

  if ( val.isMember( "memoryPolicy" ) && val["memoryPolicy"].isString() )
    meta.memoryPolicy = val["memoryPolicy"].asString();

  return meta;
}

Json::Value AlgorithmDescriptor::toInputSchema() const
{
  Json::Value root( Json::objectValue );
  root["$schema"] = "http://json-schema.org/draft-07/schema#";
  root["title"] = displayName;
  root["description"] = description;
  root["type"] = "object";

  Json::Value props( Json::objectValue );
  Json::Value reqArr( Json::arrayValue );

  for ( const auto &port : inputs )
  {
    props[port.name] = port.toJsonSchema();
    if ( port.required )
      reqArr.append( port.name );
  }

  root["properties"] = props;
  if ( !reqArr.empty() )
    root["required"] = reqArr;

  return root;
}

Json::Value AlgorithmDescriptor::toOutputSchema() const
{
  Json::Value root( Json::objectValue );
  root["type"] = "object";

  Json::Value props( Json::objectValue );
  for ( const auto &port : outputs )
  {
    props[port.name] = port.toJsonSchema();
  }

  root["properties"] = props;
  return root;
}

Json::Value AlgorithmDescriptor::toToolCallDefinition() const
{
  Json::Value root( Json::objectValue );
  root["type"] = "function";

  Json::Value funcObj( Json::objectValue );
  // Function name normalized for LLM (replace ':' with '_')
  std::string normName = id;
  for ( auto &ch : normName )
  {
    if ( ch == ':' || ch == '-' ) ch = '_';
  }
  funcObj["name"] = normName;

  // Synthesize LLM function description
  std::string fullDesc = displayName + ": " + description;
  if ( !agentMetadata.purpose.empty() )
    fullDesc += " 适用场景：" + agentMetadata.purpose;
  if ( !agentMetadata.prerequisites.empty() )
  {
    fullDesc += " 前置条件：";
    for ( size_t i = 0; i < agentMetadata.prerequisites.size(); ++i )
    {
      if ( i > 0 ) fullDesc += "；";
      fullDesc += agentMetadata.prerequisites[i];
    }
  }

  funcObj["description"] = fullDesc;

  // Parameters
  Json::Value paramsObj( Json::objectValue );
  paramsObj["type"] = "object";

  Json::Value propsObj( Json::objectValue );
  Json::Value reqArr( Json::arrayValue );

  for ( const auto &port : inputs )
  {
    propsObj[port.name] = port.toJsonSchema();
    if ( port.required )
      reqArr.append( port.name );
  }

  paramsObj["properties"] = propsObj;
  if ( !reqArr.empty() )
    paramsObj["required"] = reqArr;

  funcObj["parameters"] = paramsObj;
  root["function"] = funcObj;

  return root;
}

} // namespace sicnu::processing

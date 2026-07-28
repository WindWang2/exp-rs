#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::processing {

enum class DataType {
  Any,
  Raster,
  Vector,
  Table,
  Numeric,
  Integer,
  String,
  Boolean,
  Enum,
  BoundingBox,
  Crs,
  Json
};

std::string dataTypeToString( DataType type );
DataType dataTypeFromString( const std::string &typeStr );

struct PortDescriptor
{
  std::string name;
  std::string displayName;
  std::string description;
  DataType type = DataType::Any;
  bool required = true;
  std::string defaultValue;
  std::vector<std::string> enumOptions;

  Json::Value toJsonSchema() const;
};

struct AgentMetadata
{
  std::string purpose;
  std::vector<std::string> tags;
  std::vector<std::string> prerequisites;
  std::vector<std::string> workflowHints;
  std::vector<std::string> limitations;
  std::string llmPromptHint;

  Json::Value toJson() const;
  static AgentMetadata fromJson( const Json::Value &val );
};

struct AlgorithmDescriptor
{
  std::string id;
  std::string displayName;
  std::string group;
  std::string description;

  std::vector<PortDescriptor> inputs;
  std::vector<PortDescriptor> outputs;

  AgentMetadata agentMetadata;

  Json::Value toInputSchema() const;
  Json::Value toOutputSchema() const;
  Json::Value toToolCallDefinition() const;
};

} // namespace sicnu::processing

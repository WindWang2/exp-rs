// src/workflow/workflow_definition.cpp
#include "workflow_definition.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "placeholder_grammar.h"

namespace sicnu::workflow {

Json::Value workflowDefinitionToJson( const WorkflowDefinition &def )
{
  Json::Value root( Json::objectValue );
  root["id"] = def.id;
  root["title"] = def.title;
  root["workspaceKind"] = def.workspaceKind;
  root["host"] = static_cast<int>( def.host );

  Json::Value stepsArr( Json::arrayValue );
  for ( const auto &step : def.steps )
  {
    Json::Value stepVal( Json::objectValue );
    stepVal["id"] = step.id;
    stepVal["title"] = step.title;
    stepVal["kind"] = static_cast<int>( step.kind );
    stepVal["operatorId"] = step.operatorId;
    stepVal["artifactOnSuccess"] = step.artifactOnSuccess;

    // Spatial & Port UI Metadata
    Json::Value uiObj( Json::objectValue );
    uiObj["x"] = step.uiMeta.x;
    uiObj["y"] = step.uiMeta.y;
    if ( !step.uiMeta.portAddToMap.empty() )
    {
      Json::Value mapObj( Json::objectValue );
      for ( const auto &[pName, enabled] : step.uiMeta.portAddToMap )
      {
        mapObj[pName] = enabled;
      }
      uiObj["portAddToMap"] = mapObj;
    }
    stepVal["meta"]["ui"] = uiObj;

    // Gates
    Json::Value gatesArr( Json::arrayValue );
    for ( const auto &gate : step.gates )
    {
      Json::Value gVal( Json::objectValue );
      gVal["require"] = gate.require;
      gVal["hint"] = gate.hint;
      gatesArr.append( gVal );
    }
    stepVal["gates"] = gatesArr;

    // Inputs (Dependencies)
    Json::Value inputsArr( Json::arrayValue );
    for ( const auto &conn : step.inputs )
    {
      Json::Value connVal( Json::objectValue );
      connVal["fromStepId"] = conn.fromStepId;
      connVal["fromPort"] = conn.fromPort;
      connVal["toPort"] = conn.toPort;
      inputsArr.append( connVal );
    }
    stepVal["inputs"] = inputsArr;
    if ( !step.params.isNull() )
      stepVal["params"] = step.params;

    stepsArr.append( stepVal );
  }

  root["steps"] = stepsArr;
  return root;
}

bool workflowDefinitionFromJson( const Json::Value &json, WorkflowDefinition &def, std::string &error )
{
  if ( !json.isObject() )
  {
    error = "Invalid JSON: root must be an object";
    return false;
  }

  def.id = json.isMember( "id" ) && json["id"].isString() ? json["id"].asString() : "agent_plan";
  if ( json.isMember( "title" ) && json["title"].isString() )
    def.title = json["title"].asString();
  else if ( json.isMember( "name" ) && json["name"].isString() )
    def.title = json["name"].asString();
  else
    def.title = "Agent Plan";

  if ( json.isMember( "workspaceKind" ) && json["workspaceKind"].isString() )
    def.workspaceKind = json["workspaceKind"].asString();
  if ( json.isMember( "host" ) && json["host"].isInt() )
    def.host = static_cast<HostKind>( json["host"].asInt() );

  def.steps.clear();
  if ( json.isMember( "steps" ) && json["steps"].isArray() )
  {
    for ( const auto &stepVal : json["steps"] )
    {
      if ( !stepVal.isObject() )
        continue;

      StepDef step;
      if ( stepVal.isMember( "id" ) && stepVal["id"].isString() )
        step.id = stepVal["id"].asString();
      if ( stepVal.isMember( "title" ) && stepVal["title"].isString() )
        step.title = stepVal["title"].asString();
      if ( stepVal.isMember( "kind" ) && stepVal["kind"].isInt() )
        step.kind = static_cast<StepKind>( stepVal["kind"].asInt() );
      else
        step.kind = StepKind::Operator;

      if ( stepVal.isMember( "operatorId" ) && stepVal["operatorId"].isString() )
        step.operatorId = stepVal["operatorId"].asString();
      else if ( stepVal.isMember( "operator" ) && stepVal["operator"].isString() )
        step.operatorId = stepVal["operator"].asString();
      else if ( stepVal.isMember( "name" ) && stepVal["name"].isString() )
        step.operatorId = stepVal["name"].asString();

      if ( step.title.empty() )
        step.title = step.operatorId;

      if ( stepVal.isMember( "artifactOnSuccess" ) && stepVal["artifactOnSuccess"].isString() )
        step.artifactOnSuccess = stepVal["artifactOnSuccess"].asString();

      if ( stepVal.isMember( "meta" ) && stepVal["meta"].isMember( "ui" ) )
      {
        const auto &uiObj = stepVal["meta"]["ui"];
        if ( uiObj.isMember( "x" ) )
          step.uiMeta.x = uiObj["x"].asDouble();
        if ( uiObj.isMember( "y" ) )
          step.uiMeta.y = uiObj["y"].asDouble();
        if ( uiObj.isMember( "portAddToMap" ) && uiObj["portAddToMap"].isObject() )
        {
          const auto &mapObj = uiObj["portAddToMap"];
          for ( const auto &pName : mapObj.getMemberNames() )
          {
            step.uiMeta.portAddToMap[pName] = mapObj[pName].asBool();
          }
        }
      }

      if ( stepVal.isMember( "gates" ) && stepVal["gates"].isArray() )
      {
        for ( const auto &gVal : stepVal["gates"] )
        {
          GateDef gate;
          if ( gVal.isMember( "require" ) && gVal["require"].isString() )
            gate.require = gVal["require"].asString();
          if ( gVal.isMember( "hint" ) && gVal["hint"].isString() )
            gate.hint = gVal["hint"].asString();
          step.gates.push_back( gate );
        }
      }

      if ( stepVal.isMember( "inputs" ) && stepVal["inputs"].isArray() )
      {
        for ( const auto &connVal : stepVal["inputs"] )
        {
          StepConnection conn;
          if ( connVal.isMember( "fromStepId" ) && connVal["fromStepId"].isString() )
            conn.fromStepId = connVal["fromStepId"].asString();
          if ( connVal.isMember( "fromPort" ) && connVal["fromPort"].isString() )
            conn.fromPort = connVal["fromPort"].asString();
          if ( connVal.isMember( "toPort" ) && connVal["toPort"].isString() )
            conn.toPort = connVal["toPort"].asString();
          step.inputs.push_back( conn );
        }
      }

      if ( stepVal.isMember( "params" ) && stepVal["params"].isObject() )
        step.params = stepVal["params"];
      else if ( stepVal.isMember( "arguments" ) && stepVal["arguments"].isObject() )
        step.params = stepVal["arguments"];
      else
        step.params = Json::Value( Json::objectValue );

      if ( step.inputs.empty() && step.params.isObject() )
      {
        for ( const auto &key : step.params.getMemberNames() )
        {
          if ( !step.params[key].isString() )
            continue;
          const std::string strVal = step.params[key].asString();
          auto inferred = inferStepConnections( key, strVal );
          for ( auto &conn : inferred )
          {
            step.inputs.push_back( std::move( conn ) );
          }
        }
      }

      def.steps.push_back( step );
    }
  }

  return true;
}

bool topologicalSortSteps( const WorkflowDefinition &def, std::vector<std::string> &orderedStepIds, std::string &error )
{
  orderedStepIds.clear();
  error.clear();

  std::unordered_map<std::string, int> inDegree;
  std::unordered_map<std::string, std::vector<std::string>> graph; // fromStep -> list of toSteps
  std::unordered_map<std::string, const StepDef *> stepMap;

  for ( const auto &step : def.steps )
  {
    stepMap[step.id] = &step;
    if ( inDegree.find( step.id ) == inDegree.end() )
      inDegree[step.id] = 0;

    for ( const auto &conn : step.inputs )
    {
      if ( !conn.fromStepId.empty() )
      {
        graph[conn.fromStepId].push_back( step.id );
        inDegree[step.id]++;
      }
    }
  }

  std::queue<std::string> zeroDegreeQueue;
  // Push all nodes with 0 in-degree in original order
  for ( const auto &step : def.steps )
  {
    if ( inDegree[step.id] == 0 )
    {
      zeroDegreeQueue.push( step.id );
    }
  }

  while ( !zeroDegreeQueue.empty() )
  {
    std::string current = zeroDegreeQueue.front();
    zeroDegreeQueue.pop();
    orderedStepIds.push_back( current );

    auto it = graph.find( current );
    if ( it != graph.end() )
    {
      for ( const auto &dependentId : it->second )
      {
        inDegree[dependentId]--;
        if ( inDegree[dependentId] == 0 )
        {
          zeroDegreeQueue.push( dependentId );
        }
      }
    }
  }

  if ( orderedStepIds.size() != def.steps.size() )
  {
    error = "DAG contains a cycle or unresolved step dependency";
    return false;
  }

  return true;
}

bool validatePortConnection( const std::string &sourcePortType, const std::string &targetPortType )
{
  if ( sourcePortType.empty() || targetPortType.empty() )
    return true;

  if ( sourcePortType == "Any" || targetPortType == "Any" )
    return true;

  if ( sourcePortType == targetPortType )
    return true;

  // Compatible raster variations
  if ( ( sourcePortType == "RasterLayer" || sourcePortType == "Raster" ) &&
       ( targetPortType == "RasterLayer" || targetPortType == "Raster" ) )
    return true;

  // Compatible vector variations
  if ( ( sourcePortType == "VectorLayer" || sourcePortType == "Vector" ) &&
       ( targetPortType == "VectorLayer" || targetPortType == "Vector" ) )
    return true;

  // Compatible number variations
  if ( ( sourcePortType == "Number" || sourcePortType == "Double" || sourcePortType == "Integer" || sourcePortType == "Float" ) &&
       ( targetPortType == "Number" || targetPortType == "Double" || targetPortType == "Integer" || targetPortType == "Float" ) )
    return true;

  return false;
}

} // namespace sicnu::workflow

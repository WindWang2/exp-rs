// src/workflow/workflow_definition.cpp
#include "workflow_definition.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>

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

    stepsArr.append( stepVal );
  }

  root["steps"] = stepsArr;
  return root;
}

bool workflowDefinitionFromJson( const Json::Value &json, WorkflowDefinition &def, std::string &error )
{
  if ( !json.isObject() || !json.isMember( "id" ) )
  {
    error = "Invalid JSON: missing 'id' field";
    return false;
  }

  def.id = json["id"].asString();
  if ( json.isMember( "title" ) )
    def.title = json["title"].asString();
  if ( json.isMember( "workspaceKind" ) )
    def.workspaceKind = json["workspaceKind"].asString();
  if ( json.isMember( "host" ) )
    def.host = static_cast<HostKind>( json["host"].asInt() );

  def.steps.clear();
  if ( json.isMember( "steps" ) && json["steps"].isArray() )
  {
    for ( const auto &stepVal : json["steps"] )
    {
      StepDef step;
      if ( stepVal.isMember( "id" ) )
        step.id = stepVal["id"].asString();
      if ( stepVal.isMember( "title" ) )
        step.title = stepVal["title"].asString();
      if ( stepVal.isMember( "kind" ) )
        step.kind = static_cast<StepKind>( stepVal["kind"].asInt() );
      if ( stepVal.isMember( "operatorId" ) )
        step.operatorId = stepVal["operatorId"].asString();
      if ( stepVal.isMember( "artifactOnSuccess" ) )
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
          if ( gVal.isMember( "require" ) )
            gate.require = gVal["require"].asString();
          if ( gVal.isMember( "hint" ) )
            gate.hint = gVal["hint"].asString();
          step.gates.push_back( gate );
        }
      }

      if ( stepVal.isMember( "inputs" ) && stepVal["inputs"].isArray() )
      {
        for ( const auto &connVal : stepVal["inputs"] )
        {
          StepConnection conn;
          if ( connVal.isMember( "fromStepId" ) )
            conn.fromStepId = connVal["fromStepId"].asString();
          if ( connVal.isMember( "fromPort" ) )
            conn.fromPort = connVal["fromPort"].asString();
          if ( connVal.isMember( "toPort" ) )
            conn.toPort = connVal["toPort"].asString();
          step.inputs.push_back( conn );
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

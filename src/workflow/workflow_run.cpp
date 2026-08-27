#include "workflow_run.h"
#include "workflow_definition.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace sicnu::workflow {

namespace {

std::string currentIsoTimestamp()
{
  const auto now = std::chrono::system_clock::now();
  const auto itt = std::chrono::system_clock::to_time_t( now );
  std::ostringstream ss;
  ss << std::put_time( std::gmtime( &itt ), "%Y-%m-%dT%H:%M:%SZ" );
  return ss.str();
}

std::string generateDefaultRunId()
{
  const auto now = std::chrono::system_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( now.time_since_epoch() ).count();
  return "run-" + std::to_string( ms );
}

} // namespace

std::string workflowRunStateToString( WorkflowRunState state )
{
  switch ( state )
  {
    case WorkflowRunState::Created: return "Created";
    case WorkflowRunState::Planning: return "Planning";
    case WorkflowRunState::Ready: return "Ready";
    case WorkflowRunState::Running: return "Running";
    case WorkflowRunState::WaitingResource: return "WaitingResource";
    case WorkflowRunState::Interrupted: return "Interrupted";
    case WorkflowRunState::Cancelling: return "Cancelling";
    case WorkflowRunState::Canceled: return "Canceled";
    case WorkflowRunState::Failed: return "Failed";
    case WorkflowRunState::Completed: return "Completed";
  }
  return "Created";
}

WorkflowRunState workflowRunStateFromString( const std::string &str )
{
  if ( str == "Created" ) return WorkflowRunState::Created;
  if ( str == "Planning" ) return WorkflowRunState::Planning;
  if ( str == "Ready" ) return WorkflowRunState::Ready;
  if ( str == "Running" ) return WorkflowRunState::Running;
  if ( str == "WaitingResource" ) return WorkflowRunState::WaitingResource;
  if ( str == "Interrupted" ) return WorkflowRunState::Interrupted;
  if ( str == "Cancelling" ) return WorkflowRunState::Cancelling;
  if ( str == "Canceled" ) return WorkflowRunState::Canceled;
  if ( str == "Failed" ) return WorkflowRunState::Failed;
  if ( str == "Completed" ) return WorkflowRunState::Completed;
  return WorkflowRunState::Created;
}

bool isTerminalRunState( WorkflowRunState state )
{
  return state == WorkflowRunState::Completed
         || state == WorkflowRunState::Failed
         || state == WorkflowRunState::Canceled
         || state == WorkflowRunState::Interrupted;
}

bool isValidRunStateTransition( WorkflowRunState from, WorkflowRunState to )
{
  if ( from == to )
    return true;

  switch ( from )
  {
    case WorkflowRunState::Created:
      return to == WorkflowRunState::Planning
             || to == WorkflowRunState::Ready
             || to == WorkflowRunState::Canceled;

    case WorkflowRunState::Planning:
      return to == WorkflowRunState::Ready
             || to == WorkflowRunState::Running
             || to == WorkflowRunState::Failed
             || to == WorkflowRunState::Interrupted
             || to == WorkflowRunState::Canceled;

    case WorkflowRunState::Ready:
      return to == WorkflowRunState::Running
             || to == WorkflowRunState::WaitingResource
             || to == WorkflowRunState::Interrupted
             || to == WorkflowRunState::Canceled
             || to == WorkflowRunState::Failed;

    case WorkflowRunState::WaitingResource:
      return to == WorkflowRunState::Ready
             || to == WorkflowRunState::Running
             || to == WorkflowRunState::Interrupted
             || to == WorkflowRunState::Canceled
             || to == WorkflowRunState::Failed;

    case WorkflowRunState::Running:
      return to == WorkflowRunState::WaitingResource
             || to == WorkflowRunState::Completed
             || to == WorkflowRunState::Failed
             || to == WorkflowRunState::Interrupted
             || to == WorkflowRunState::Cancelling
             || to == WorkflowRunState::Canceled;

    case WorkflowRunState::Cancelling:
      return to == WorkflowRunState::Canceled
             || to == WorkflowRunState::Failed;

    case WorkflowRunState::Interrupted:
      return to == WorkflowRunState::Planning
             || to == WorkflowRunState::Ready
             || to == WorkflowRunState::Running
             || to == WorkflowRunState::Canceled
             || to == WorkflowRunState::Failed;

    case WorkflowRunState::Canceled:
    case WorkflowRunState::Failed:
    case WorkflowRunState::Completed:
      return false; // Terminal states
  }
  return false;
}

Json::Value StepPlan::toJson() const
{
  Json::Value root( Json::objectValue );
  root["stepId"] = stepId;
  root["operatorId"] = operatorId;
  root["kind"] = static_cast<int>( kind );
  root["rawParams"] = rawParams;
  root["resolvedParams"] = resolvedParams;

  Json::Value depsArr( Json::arrayValue );
  for ( const auto &dep : dependencies )
    depsArr.append( dep );
  root["dependencies"] = depsArr;

  root["resourceEstimateMb"] = resourceEstimateMb;
  root["fingerprint"] = fingerprint;
  root["cacheHit"] = cacheHit;
  root["cachedOutputAssetId"] = cachedOutputAssetId;
  root["status"] = status;
  root["taskId"] = static_cast<Json::Int64>( taskId );
  root["resultPayload"] = resultPayload;
  root["outputLayerPath"] = outputLayerPath;
  root["errorMessage"] = errorMessage;
  root["startTime"] = startTime;
  root["endTime"] = endTime;
  return root;
}

StepPlan StepPlan::fromJson( const Json::Value &json )
{
  StepPlan plan;
  if ( !json.isObject() )
    return plan;

  if ( json.isMember( "stepId" ) && json["stepId"].isString() )
    plan.stepId = json["stepId"].asString();
  if ( json.isMember( "operatorId" ) && json["operatorId"].isString() )
    plan.operatorId = json["operatorId"].asString();
  if ( json.isMember( "kind" ) && json["kind"].isInt() )
    plan.kind = static_cast<StepKind>( json["kind"].asInt() );
  if ( json.isMember( "rawParams" ) )
    plan.rawParams = json["rawParams"];
  if ( json.isMember( "resolvedParams" ) )
    plan.resolvedParams = json["resolvedParams"];

  if ( json.isMember( "dependencies" ) && json["dependencies"].isArray() )
  {
    for ( const auto &item : json["dependencies"] )
    {
      if ( item.isString() )
        plan.dependencies.push_back( item.asString() );
    }
  }

  if ( json.isMember( "resourceEstimateMb" ) && json["resourceEstimateMb"].isUInt() )
    plan.resourceEstimateMb = json["resourceEstimateMb"].asUInt();
  if ( json.isMember( "fingerprint" ) && json["fingerprint"].isString() )
    plan.fingerprint = json["fingerprint"].asString();
  if ( json.isMember( "cacheHit" ) && json["cacheHit"].isBool() )
    plan.cacheHit = json["cacheHit"].asBool();
  if ( json.isMember( "cachedOutputAssetId" ) && json["cachedOutputAssetId"].isString() )
    plan.cachedOutputAssetId = json["cachedOutputAssetId"].asString();
  if ( json.isMember( "status" ) && json["status"].isString() )
    plan.status = json["status"].asString();
  if ( json.isMember( "taskId" ) && json["taskId"].isInt64() )
    plan.taskId = static_cast<long>( json["taskId"].asInt64() );
  if ( json.isMember( "resultPayload" ) )
    plan.resultPayload = json["resultPayload"];
  if ( json.isMember( "outputLayerPath" ) && json["outputLayerPath"].isString() )
    plan.outputLayerPath = json["outputLayerPath"].asString();
  if ( json.isMember( "errorMessage" ) && json["errorMessage"].isString() )
    plan.errorMessage = json["errorMessage"].asString();
  if ( json.isMember( "startTime" ) && json["startTime"].isString() )
    plan.startTime = json["startTime"].asString();
  if ( json.isMember( "endTime" ) && json["endTime"].isString() )
    plan.endTime = json["endTime"].asString();

  return plan;
}

std::unique_ptr<WorkflowRun> WorkflowRun::createFromDefinition( const WorkflowDefinition &def,
                                                                const std::string &runId )
{
  auto run = std::make_unique<WorkflowRun>();
  run->m_runId = runId.empty() ? generateDefaultRunId() : runId;
  run->m_workflowId = def.id;
  run->m_definition = def;
  run->m_state = WorkflowRunState::Created;
  run->m_createdAt = currentIsoTimestamp();
  run->m_updatedAt = run->m_createdAt;

  run->m_stepPlans.reserve( def.steps.size() );
  for ( const auto &stepDef : def.steps )
  {
    StepPlan plan;
    plan.stepId = stepDef.id;
    plan.operatorId = stepDef.operatorId;
    plan.kind = stepDef.kind;
    plan.rawParams = stepDef.params;
    plan.resolvedParams = stepDef.params;
    plan.resourceEstimateMb = stepDef.resourceEstimateMb;
    for ( const auto &conn : stepDef.inputs )
    {
      if ( !conn.fromStepId.empty() )
        plan.dependencies.push_back( conn.fromStepId );
    }
    plan.status = "Pending";
    run->m_stepPlans.push_back( plan );
  }

  return run;
}

bool WorkflowRun::transitionTo( WorkflowRunState newState )
{
  if ( !isValidRunStateTransition( m_state, newState ) )
    return false;

  m_state = newState;
  m_updatedAt = currentIsoTimestamp();
  return true;
}

StepPlan *WorkflowRun::findStepPlan( const std::string &stepId )
{
  for ( auto &plan : m_stepPlans )
  {
    if ( plan.stepId == stepId )
      return &plan;
  }
  return nullptr;
}

const StepPlan *WorkflowRun::findStepPlan( const std::string &stepId ) const
{
  for ( const auto &plan : m_stepPlans )
  {
    if ( plan.stepId == stepId )
      return &plan;
  }
  return nullptr;
}

std::string WorkflowRun::getArtifact( const std::string &name ) const
{
  const auto it = m_artifacts.find( name );
  if ( it != m_artifacts.end() )
    return it->second;
  return "";
}

void WorkflowRun::recalculateProgress()
{
  if ( m_stepPlans.empty() )
  {
    m_progress = ( m_state == WorkflowRunState::Completed ) ? 1.0 : 0.0;
    return;
  }

  int completedCount = 0;
  for ( const auto &plan : m_stepPlans )
  {
    if ( plan.status == "Completed" || plan.status == "Skipped" )
      ++completedCount;
  }
  m_progress = static_cast<double>( completedCount ) / static_cast<double>( m_stepPlans.size() );
}

Json::Value WorkflowRun::toJson() const
{
  Json::Value root( Json::objectValue );
  root["runId"] = m_runId;
  root["workflowId"] = m_workflowId;
  root["state"] = workflowRunStateToString( m_state );
  root["errorMessage"] = m_errorMessage;
  root["progress"] = m_progress;
  root["createdAt"] = m_createdAt;
  root["updatedAt"] = m_updatedAt;

  // Serialize definition
  root["definition"] = workflowDefinitionToJson( m_definition );

  // Serialize step plans
  Json::Value stepsArr( Json::arrayValue );
  for ( const auto &plan : m_stepPlans )
    stepsArr.append( plan.toJson() );
  root["stepPlans"] = stepsArr;

  // Serialize artifacts
  Json::Value artObj( Json::objectValue );
  for ( const auto &[k, v] : m_artifacts )
    artObj[k] = v;
  root["artifacts"] = artObj;

  return root;
}

std::unique_ptr<WorkflowRun> WorkflowRun::fromJson( const Json::Value &json, std::string &error )
{
  if ( !json.isObject() )
  {
    error = "Invalid JSON: WorkflowRun root must be an object";
    return nullptr;
  }

  auto run = std::make_unique<WorkflowRun>();

  if ( json.isMember( "runId" ) && json["runId"].isString() )
    run->m_runId = json["runId"].asString();
  if ( json.isMember( "workflowId" ) && json["workflowId"].isString() )
    run->m_workflowId = json["workflowId"].asString();
  if ( json.isMember( "state" ) && json["state"].isString() )
    run->m_state = workflowRunStateFromString( json["state"].asString() );
  if ( json.isMember( "errorMessage" ) && json["errorMessage"].isString() )
    run->m_errorMessage = json["errorMessage"].asString();
  if ( json.isMember( "progress" ) && json["progress"].isNumeric() )
    run->m_progress = json["progress"].asDouble();
  if ( json.isMember( "createdAt" ) && json["createdAt"].isString() )
    run->m_createdAt = json["createdAt"].asString();
  if ( json.isMember( "updatedAt" ) && json["updatedAt"].isString() )
    run->m_updatedAt = json["updatedAt"].asString();

  if ( json.isMember( "definition" ) && json["definition"].isObject() )
  {
    std::string defError;
    if ( !workflowDefinitionFromJson( json["definition"], run->m_definition, defError ) )
    {
      error = "Failed to parse WorkflowDefinition: " + defError;
      return nullptr;
    }
  }

  if ( json.isMember( "stepPlans" ) && json["stepPlans"].isArray() )
  {
    for ( const auto &stepJson : json["stepPlans"] )
      run->m_stepPlans.push_back( StepPlan::fromJson( stepJson ) );
  }

  if ( json.isMember( "artifacts" ) && json["artifacts"].isObject() )
  {
    for ( const auto &name : json["artifacts"].getMemberNames() )
    {
      if ( json["artifacts"][name].isString() )
        run->m_artifacts[name] = json["artifacts"][name].asString();
    }
  }

  return run;
}

} // namespace sicnu::workflow

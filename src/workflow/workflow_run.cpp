#include "workflow_run.h"
#include "workflow_definition.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <random>
#include <sstream>
#include <algorithm>

namespace sicnu::workflow {

namespace {

std::string currentIsoTimestamp()
{
  const auto now = std::chrono::system_clock::now();
  const auto itt = std::chrono::system_clock::to_time_t( now );
  std::tm tmBuf{};
  // gmtime returns a shared static buffer - not safe when two runs are
  // touched from different threads (checkpoint recovery vs. background run).
  gmtime_r( &itt, &tmBuf );
  std::ostringstream ss;
  ss << std::put_time( &tmBuf, "%Y-%m-%dT%H:%M:%SZ" );
  return ss.str();
}

std::string generateDefaultRunId()
{
  // Milliseconds alone collide when two runs are created within the same
  // millisecond; a process-wide sequence plus random suffix keeps ids unique
  // across threads and across processes started in the same millisecond.
  static std::atomic<uint64_t> s_sequence{ 0 };
  const auto now = std::chrono::system_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( now.time_since_epoch() ).count();
  const uint64_t seq = s_sequence.fetch_add( 1 ) + 1;
  std::random_device rd;
  const uint32_t noise = ( rd() ^ static_cast<uint32_t>( seq << 16 ) );
  char suffix[16];
  std::snprintf( suffix, sizeof( suffix ), "%08x", noise );
  return "run-" + std::to_string( ms ) + "-" + std::to_string( seq ) + "-" + suffix;
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
  WorkflowRunState state = WorkflowRunState::Created;
  tryParseWorkflowRunState( str, state );
  return state;
}

bool tryParseWorkflowRunState( const std::string &str, WorkflowRunState &out )
{
  if ( str == "Created" ) { out = WorkflowRunState::Created; return true; }
  if ( str == "Planning" ) { out = WorkflowRunState::Planning; return true; }
  if ( str == "Ready" ) { out = WorkflowRunState::Ready; return true; }
  if ( str == "Running" ) { out = WorkflowRunState::Running; return true; }
  if ( str == "WaitingResource" ) { out = WorkflowRunState::WaitingResource; return true; }
  if ( str == "Interrupted" ) { out = WorkflowRunState::Interrupted; return true; }
  if ( str == "Cancelling" ) { out = WorkflowRunState::Cancelling; return true; }
  if ( str == "Canceled" ) { out = WorkflowRunState::Canceled; return true; }
  if ( str == "Failed" ) { out = WorkflowRunState::Failed; return true; }
  if ( str == "Completed" ) { out = WorkflowRunState::Completed; return true; }
  return false;
}

bool isValidRunId( const std::string &runId )
{
  if ( runId.empty() || runId.size() > 128 )
    return false;
  if ( runId.front() == '.' )
    return false;
  for ( const char c : runId )
  {
    const bool ok = ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' )
                    || ( c >= '0' && c <= '9' ) || c == '.' || c == '_' || c == '-';
    if ( !ok )
      return false;
  }
  return true;
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
  root["cachedOutputPath"] = cachedOutputPath;
  root["status"] = status;
  root["taskId"] = static_cast<Json::Int64>( taskId );
  root["resultPayload"] = resultPayload;
  root["outputLayerPath"] = outputLayerPath;
  root["errorMessage"] = errorMessage;
  root["startTime"] = startTime;
  root["endTime"] = endTime;
  return root;
}

StepPlan StepPlan::fromJson( const Json::Value &json, std::string *error )
{
  StepPlan plan;
  if ( !json.isObject() )
  {
    if ( error )
      *error = "StepPlan root must be an object";
    return plan;
  }

  if ( json.isMember( "stepId" ) && json["stepId"].isString() )
    plan.stepId = json["stepId"].asString();
  if ( json.isMember( "operatorId" ) && json["operatorId"].isString() )
    plan.operatorId = json["operatorId"].asString();
  if ( json.isMember( "kind" ) && json["kind"].isInt() )
  {
    const int kindValue = json["kind"].asInt();
    if ( kindValue < static_cast<int>( StepKind::Operator )
         || kindValue > static_cast<int>( StepKind::Composite ) )
    {
      if ( error )
        *error = "StepPlan '" + plan.stepId + "': invalid kind value "
                 + std::to_string( kindValue );
      return plan;
    }
    plan.kind = static_cast<StepKind>( kindValue );
  }
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
  if ( json.isMember( "cachedOutputPath" ) && json["cachedOutputPath"].isString() )
    plan.cachedOutputPath = json["cachedOutputPath"].asString();
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
  if ( !runId.empty() && !isValidRunId( runId ) )
    return nullptr; // caller-provided ids must be filename-safe (checkpoint paths derive from them)

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

std::string WorkflowRun::runId() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_runId;
}

bool WorkflowRun::setRunId( const std::string &id )
{
  if ( !isValidRunId( id ) )
    return false;
  std::lock_guard<std::mutex> lock( m_mutex );
  m_runId = id;
  touchLocked();
  return true;
}

std::string WorkflowRun::workflowId() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_workflowId;
}

void WorkflowRun::setWorkflowId( const std::string &id )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_workflowId = id;
  touchLocked();
}

WorkflowRunState WorkflowRun::state() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_state;
}

bool WorkflowRun::transitionTo( WorkflowRunState newState )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  if ( !isValidRunStateTransition( m_state, newState ) )
    return false;

  m_state = newState;
  touchLocked();
  return true;
}

void WorkflowRun::forceSetState( WorkflowRunState state_ )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_state = state_;
  touchLocked();
}

const WorkflowDefinition &WorkflowRun::definition() const
{
  return m_definition; // escapes the internal lock: single-writer model
}

void WorkflowRun::setDefinition( const WorkflowDefinition &def )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_definition = def;
  touchLocked();
}

std::vector<StepPlan> WorkflowRun::stepPlans() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_stepPlans;
}

void WorkflowRun::setStepPlans( const std::vector<StepPlan> &plans )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_stepPlans = plans;
  recalculateProgressLocked();
  touchLocked();
}

std::optional<StepPlan> WorkflowRun::stepPlan( const std::string &stepId ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  for ( const auto &plan : m_stepPlans )
  {
    if ( plan.stepId == stepId )
      return plan;
  }
  return std::nullopt;
}

bool WorkflowRun::updateStepPlan( const StepPlan &plan )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  StepPlan *existing = findStepPlanLocked( plan.stepId );
  if ( !existing )
    return false;
  *existing = plan;
  recalculateProgressLocked();
  touchLocked();
  return true;
}

bool WorkflowRun::setStepStatus( const std::string &stepId, const std::string &status )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  StepPlan *plan = findStepPlanLocked( stepId );
  if ( !plan )
    return false;
  if ( plan->status == status )
    return true;
  plan->status = status;
  recalculateProgressLocked();
  touchLocked();
  return true;
}

StepPlan *WorkflowRun::findStepPlan( const std::string &stepId )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return findStepPlanLocked( stepId ); // escapes the lock: single-writer model
}

const StepPlan *WorkflowRun::findStepPlan( const std::string &stepId ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  for ( const auto &plan : m_stepPlans )
  {
    if ( plan.stepId == stepId )
      return &plan;
  }
  return nullptr;
}

StepPlan *WorkflowRun::findStepPlanLocked( const std::string &stepId )
{
  for ( auto &plan : m_stepPlans )
  {
    if ( plan.stepId == stepId )
      return &plan;
  }
  return nullptr;
}

std::map<std::string, std::string> WorkflowRun::artifacts() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_artifacts;
}

void WorkflowRun::setArtifact( const std::string &name, const std::string &value )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_artifacts[name] = value;
  touchLocked();
}

std::string WorkflowRun::artifact( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_artifacts.find( name );
  return it != m_artifacts.end() ? it->second : std::string();
}

std::string WorkflowRun::errorMessage() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_errorMessage;
}

void WorkflowRun::setErrorMessage( const std::string &msg )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_errorMessage = msg;
  touchLocked();
}

double WorkflowRun::progress() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_progress;
}

void WorkflowRun::setProgress( double p )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_progress = p;
  touchLocked();
}

void WorkflowRun::recalculateProgress()
{
  std::lock_guard<std::mutex> lock( m_mutex );
  recalculateProgressLocked();
}

std::string WorkflowRun::createdAt() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_createdAt;
}

void WorkflowRun::setCreatedAt( const std::string &ts )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_createdAt = ts;
}

std::string WorkflowRun::updatedAt() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_updatedAt;
}

void WorkflowRun::setUpdatedAt( const std::string &ts )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_updatedAt = ts;
}

void WorkflowRun::touchLocked()
{
  m_updatedAt = currentIsoTimestamp();
}

void WorkflowRun::recalculateProgressLocked()
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
  std::lock_guard<std::mutex> lock( m_mutex );

  Json::Value root( Json::objectValue );
  root["version"] = kWorkflowRunSerializationVersion;
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

  if ( !json.isMember( "version" ) || !json["version"].isInt() )
  {
    error = "Invalid checkpoint: missing serialization version";
    return nullptr;
  }
  if ( json["version"].asInt() != kWorkflowRunSerializationVersion )
  {
    error = "Unsupported checkpoint version " + std::to_string( json["version"].asInt() )
            + " (expected " + std::to_string( kWorkflowRunSerializationVersion ) + ")";
    return nullptr;
  }

  if ( !json.isMember( "runId" ) || !json["runId"].isString()
       || !isValidRunId( json["runId"].asString() ) )
  {
    error = "Invalid checkpoint: missing or unsafe runId";
    return nullptr;
  }

  if ( !json.isMember( "definition" ) || !json["definition"].isObject() )
  {
    error = "Invalid checkpoint: missing definition";
    return nullptr;
  }

  if ( !json.isMember( "stepPlans" ) || !json["stepPlans"].isArray() )
  {
    error = "Invalid checkpoint: missing stepPlans";
    return nullptr;
  }

  auto run = std::make_unique<WorkflowRun>();

  run->m_runId = json["runId"].asString();
  if ( json.isMember( "workflowId" ) && json["workflowId"].isString() )
    run->m_workflowId = json["workflowId"].asString();
  if ( json.isMember( "state" ) && json["state"].isString() )
  {
    if ( !tryParseWorkflowRunState( json["state"].asString(), run->m_state ) )
    {
      error = "Invalid checkpoint: unknown run state '" + json["state"].asString() + "'";
      return nullptr;
    }
  }
  if ( json.isMember( "errorMessage" ) && json["errorMessage"].isString() )
    run->m_errorMessage = json["errorMessage"].asString();
  if ( json.isMember( "progress" ) && json["progress"].isNumeric() )
    run->m_progress = json["progress"].asDouble();
  if ( json.isMember( "createdAt" ) && json["createdAt"].isString() )
    run->m_createdAt = json["createdAt"].asString();
  if ( json.isMember( "updatedAt" ) && json["updatedAt"].isString() )
    run->m_updatedAt = json["updatedAt"].asString();

  {
    std::string defError;
    if ( !workflowDefinitionFromJson( json["definition"], run->m_definition, defError ) )
    {
      error = "Failed to parse WorkflowDefinition: " + defError;
      return nullptr;
    }
  }

  for ( const auto &stepJson : json["stepPlans"] )
  {
    std::string stepError;
    StepPlan plan = StepPlan::fromJson( stepJson, &stepError );
    if ( !stepError.empty() )
    {
      error = "Failed to parse StepPlan: " + stepError;
      return nullptr;
    }
    run->m_stepPlans.push_back( std::move( plan ) );
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

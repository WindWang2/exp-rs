// src/agent_ops/task_center_executor.cpp
#include "agent_ops/task_center_executor.h"
#include "processing/framework/task_center.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace sicnu::agent_ops {

long TaskCenterExecutor::parseTaskId( const std::string &runId )
{
    if ( runId.empty() )
        return -1;
    if ( runId.rfind( "task-", 0 ) == 0 )
    {
        try {
            size_t idx = 0;
            std::string sub = runId.substr( 5 );
            if ( sub.empty() ) return -1;
            long val = std::stol( sub, &idx );
            if ( idx == sub.size() ) return val;
        } catch ( ... ) {
            return -1;
        }
    }
    try {
        size_t idx = 0;
        long val = std::stol( runId, &idx );
        if ( idx == runId.size() ) return val;
    } catch ( ... ) {
        return -1;
    }
    return -1;
}

sicnu::agent_loop::ExecutionStart TaskCenterExecutor::begin( const sicnu::agent_loop::PlanDraft &plan )
{
    using sicnu::agent_loop::ExecutionStart;

    if ( plan.steps.empty() )
    {
        return ExecutionStart{
            .runId = "",
            .started = false,
            .error = "EMPTY_PLAN_REFUSED"
        };
    }

    if ( plan.steps.size() > 1000 )
    {
        return ExecutionStart{
            .runId = "",
            .started = false,
            .error = "PLAN_STEP_LIMIT_EXCEEDED"
        };
    }

    // Step validation: operator ID
    for ( const auto &step : plan.steps )
    {
        std::string opId;
        if ( step.isMember( "operator_id" ) && step[ "operator_id" ].isString() )
            opId = step[ "operator_id" ].asString();
        else if ( step.isMember( "operatorId" ) && step[ "operatorId" ].isString() )
            opId = step[ "operatorId" ].asString();
        else if ( step.isMember( "algorithm_id" ) && step[ "algorithm_id" ].isString() )
            opId = step[ "algorithm_id" ].asString();
        else if ( step.isMember( "algorithmId" ) && step[ "algorithmId" ].isString() )
            opId = step[ "algorithmId" ].asString();

        if ( opId.empty() )
        {
            return ExecutionStart{
                .runId = "",
                .started = false,
                .error = "INVALID_OPERATOR_ID"
            };
        }
        // No per-step "arguments_invalid" flag is checked here: no planner
        // contract in this tree produces one (planners report invalid plans
        // through PlanDraft.valid/error, which the session refuses before
        // execute), so the former guard was unreachable from production.
        // (#1451)
    }

    // Cyclic dependency check
    std::map<std::string, std::vector<std::string>> adj;
    std::set<std::string> allIds;
    for ( const auto &step : plan.steps )
    {
        std::string id;
        if ( step.isMember( "id" ) && step[ "id" ].isString() ) id = step[ "id" ].asString();
        else if ( step.isMember( "step_id" ) && step[ "step_id" ].isString() ) id = step[ "step_id" ].asString();
        if ( !id.empty() ) {
            allIds.insert( id );
            std::vector<std::string> deps;
            auto addDeps = [&]( const Json::Value &d ) {
                if ( d.isArray() ) {
                    for ( const auto &item : d ) if ( item.isString() ) deps.push_back( item.asString() );
                } else if ( d.isString() ) {
                    deps.push_back( d.asString() );
                }
            };
            if ( step.isMember( "dependencies" ) ) addDeps( step[ "dependencies" ] );
            if ( step.isMember( "depends_on" ) ) addDeps( step[ "depends_on" ] );
            adj[ id ] = deps;
        }
    }
    if ( !allIds.empty() ) {
        std::map<std::string, int> state; // 0=unvisited, 1=visiting, 2=visited
        std::function<bool(const std::string&)> hasCycle = [&]( const std::string &u ) -> bool {
            state[ u ] = 1;
            auto it = adj.find( u );
            if ( it != adj.end() ) {
                for ( const auto &v : it->second ) {
                    if ( state[ v ] == 1 ) return true;
                    if ( state[ v ] == 0 && hasCycle( v ) ) return true;
                }
            }
            state[ u ] = 2;
            return false;
        };
        for ( const auto &id : allIds ) {
            if ( state[ id ] == 0 && hasCycle( id ) ) {
                return ExecutionStart{
                    .runId = "",
                    .started = false,
                    .error = "CYCLIC_DEPENDENCY_DETECTED"
                };
            }
        }
    }

    // Fail closed on multi-step plans: this executor submits exactly ONE
    // TaskCenter job per plan (the first step); letting a multi-step plan
    // through would silently run only step 1 while poll() reports the
    // single job's terminal state as the whole plan's outcome. Refuse with
    // a typed code until a real multi-step submission path exists; the
    // single-step path is unchanged. (#1451)
    if ( plan.steps.size() > 1 )
    {
        return ExecutionStart{
            .runId = "",
            .started = false,
            .error = "MULTI_STEP_PLAN_UNSUPPORTED"
        };
    }

    // Construct JobRequest with source="agent" to ensure LatencyClass::Interactive (rank 0)
    sicnu::jobs::JobRequest request;
    request.source = "agent";
    request.title = !plan.intent.empty() ? plan.intent : ("Plan " + plan.planId);
    request.clientTag = "agent_plan:" + plan.planId;
    request.priority = 1; // Normal

    const auto &firstStep = plan.steps[ 0 ];
    std::string opId;
    if ( firstStep.isMember( "operator_id" ) && firstStep[ "operator_id" ].isString() )
        opId = firstStep[ "operator_id" ].asString();
    else if ( firstStep.isMember( "operatorId" ) && firstStep[ "operatorId" ].isString() )
        opId = firstStep[ "operatorId" ].asString();
    else if ( firstStep.isMember( "algorithm_id" ) && firstStep[ "algorithm_id" ].isString() )
        opId = firstStep[ "algorithm_id" ].asString();
    else if ( firstStep.isMember( "algorithmId" ) && firstStep[ "algorithmId" ].isString() )
        opId = firstStep[ "algorithmId" ].asString();
    request.algorithmId = opId;

    if ( firstStep.isMember( "arguments" ) && firstStep[ "arguments" ].isObject() )
        request.params = firstStep[ "arguments" ];
    else if ( firstStep.isMember( "params" ) && firstStep[ "params" ].isObject() )
        request.params = firstStep[ "params" ];
    else
        request.params = Json::Value( Json::objectValue );

    // Single-step contract (see the MULTI_STEP_PLAN_UNSUPPORTED refusal
    // above): only the plan identity rides along, never smuggled steps.
    request.params[ "plan_id" ] = plan.planId;

    const long taskId = sicnu::TaskCenter::instance().submitJob( request );
    if ( taskId <= 0 )
    {
        std::string details;
        if ( sicnu::TaskCenter::instance().isShuttingDown() )
        {
            details = "SHUTTING_DOWN";
        }
        else
        {
            details = "QUEUE_SATURATED";
        }
        return ExecutionStart{
            .runId = "",
            .started = false,
            .error = "ADMISSION_REFUSED: " + details
        };
    }

    return ExecutionStart{
        .runId = "task-" + std::to_string( taskId ),
        .started = true,
        .error = ""
    };
}

sicnu::agent_loop::ExecutionOutcome TaskCenterExecutor::poll( const sicnu::agent_loop::ExecutionStart &start,
                                                              long long timeoutMs )
{
    using sicnu::agent_loop::ExecutionOutcome;

    if ( !start.started || start.runId.empty() )
    {
        ExecutionOutcome outcome;
        outcome.runId = start.runId;
        outcome.finished = true;
        outcome.succeeded = false;
        outcome.state = ExecutionState::kFailed;
        outcome.errorCode = "INVALID_RUN_ID";
        outcome.errorMessage = start.error.empty() ? "Invalid or empty runId" : start.error;
        return outcome;
    }

    const long taskId = parseTaskId( start.runId );
    if ( taskId <= 0 )
    {
        ExecutionOutcome outcome;
        outcome.runId = start.runId;
        outcome.finished = true;
        outcome.succeeded = false;
        outcome.state = ExecutionState::kFailed;
        outcome.errorCode = "INVALID_RUN_ID";
        outcome.errorMessage = "Failed to parse task ID from " + start.runId;
        return outcome;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( std::max( 0LL, timeoutMs ) );
    while ( true )
    {
        const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
        if ( info.taskId < 0 )
        {
            ExecutionOutcome outcome;
            outcome.runId = start.runId;
            outcome.finished = true;
            outcome.succeeded = false;
            outcome.state = ExecutionState::kFailed;
            outcome.errorCode = "TASK_NOT_FOUND";
            outcome.errorMessage = "Task " + std::to_string( taskId ) + " not found";
            return outcome;
        }

        if ( info.status == sicnu::TaskStatus::Completed )
        {
            ExecutionOutcome outcome;
            outcome.runId = start.runId;
            outcome.finished = true;
            outcome.succeeded = true;
            outcome.state = ExecutionState::kCompleted;
            outcome.resultPayload = info.resultPayload;
            if ( !info.outputLayerPath.isEmpty() )
            {
                outcome.artifacts.push_back( info.outputLayerPath.toStdString() );
            }
            if ( info.resultPayload.isMember( "artifacts" ) && info.resultPayload[ "artifacts" ].isArray() )
            {
                for ( const auto &art : info.resultPayload[ "artifacts" ] )
                {
                    if ( art.isString() )
                        outcome.artifacts.push_back( art.asString() );
                }
            }
            return outcome;
        }
        else if ( info.status == sicnu::TaskStatus::Canceled )
        {
            ExecutionOutcome outcome;
            outcome.runId = start.runId;
            outcome.finished = true;
            outcome.succeeded = false;
            outcome.state = ExecutionState::kCancelled;
            outcome.errorCode = "CANCELLED";
            outcome.errorMessage = info.errorMessage.isEmpty() ? "Task was cancelled" : info.errorMessage.toStdString();
            return outcome;
        }
        else if ( info.status == sicnu::TaskStatus::Failed )
        {
            ExecutionOutcome outcome;
            outcome.runId = start.runId;
            outcome.finished = true;
            outcome.succeeded = false;
            outcome.state = ExecutionState::kFailed;
            outcome.errorCode = "EXECUTION_FAILED";
            outcome.errorMessage = info.errorMessage.isEmpty() ? "Task execution failed" : info.errorMessage.toStdString();
            return outcome;
        }

        if ( std::chrono::steady_clock::now() >= deadline )
        {
            ExecutionOutcome outcome;
            outcome.runId = start.runId;
            outcome.finished = false;
            outcome.succeeded = false;
            outcome.state = ExecutionState::kRunning;
            return outcome;
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
}

sicnu::agent_loop::ExecutionOutcome TaskCenterExecutor::poll( const std::string &runId,
                                                              long long timeoutMs )
{
    sicnu::agent_loop::ExecutionStart start;
    start.runId = runId;
    start.started = !runId.empty();
    return poll( start, timeoutMs );
}

void TaskCenterExecutor::cancel( const sicnu::agent_loop::ExecutionStart &start )
{
    cancel( start.runId );
}

void TaskCenterExecutor::cancel( const std::string &runId )
{
    if ( runId.empty() )
        return;
    const long taskId = parseTaskId( runId );
    if ( taskId <= 0 )
        return;
    sicnu::TaskCenter::instance().cancelTask( taskId, sicnu::TaskCancelReason::User );
}

} // namespace sicnu::agent_ops

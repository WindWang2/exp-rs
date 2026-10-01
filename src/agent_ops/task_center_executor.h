// src/agent_ops/task_center_executor.h
#pragma once

#include "agent_loop/session_seams.h"
#include <string>

namespace sicnu::agent_ops {

namespace ExecutionState {
inline constexpr const char *kRunning = "running";
inline constexpr const char *kCompleted = "succeeded";
inline constexpr const char *kFailed = "failed";
inline constexpr const char *kCancelled = "cancelled";
} // namespace ExecutionState

class TaskCenterExecutor : public sicnu::agent_loop::IExecutor {
public:
    TaskCenterExecutor() = default;
    ~TaskCenterExecutor() override = default;

    /// Converts plan into a TaskCenter job submission with source="agent" (Interactive latency class).
    /// If TaskCenter::submitJob returns <= 0, returns started=false with typed refusal details.
    sicnu::agent_loop::ExecutionStart begin( const sicnu::agent_loop::PlanDraft &plan ) override;

    /// Queries TaskCenter task status, mapping to ExecutionState.
    /// Bounded wait; returns non-finished outcome only on timeout.
    sicnu::agent_loop::ExecutionOutcome poll( const sicnu::agent_loop::ExecutionStart &start,
                                              long long timeoutMs = 1000 ) override;

    /// Convenience overload taking runId string.
    sicnu::agent_loop::ExecutionOutcome poll( const std::string &runId,
                                              long long timeoutMs = 1000 );

    /// Cancels task in TaskCenter respecting R7 cancellation safety.
    void cancel( const sicnu::agent_loop::ExecutionStart &start ) override;

    /// Convenience overload taking runId string.
    void cancel( const std::string &runId );

    /// Helper to extract numeric task ID from runId ("task-<id>" or "<id>").
    static long parseTaskId( const std::string &runId );
};

} // namespace sicnu::agent_ops

namespace sicnu::agent {
using TaskCenterExecutor = sicnu::agent_ops::TaskCenterExecutor;
} // namespace sicnu::agent

namespace sicnu::agent_loop {
using TaskCenterExecutor = sicnu::agent_ops::TaskCenterExecutor;
} // namespace sicnu::agent_loop

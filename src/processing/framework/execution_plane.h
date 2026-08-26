// src/processing/framework/execution_plane.h
//
// Unified Execution Plane — the single async-first execution abstraction shared
// by every entry surface (GUI dialogs, Agent Copilot, MCP server, Pi bridge,
// workflows, CLI). It is a thin, thread-safe layer ON TOP of the existing
// spine (TaskCenter → JobEngine → operator/adapter), not a second scheduler:
//
//   entry (GUI / Agent / MCP / Pi / Workflow / CLI)
//     → ExecutionRequest            (what to run, entry tag, budget, timeout)
//     → ExecutionPlane::submit      (validation stays with the callers)
//     → TaskCenter admission        (WaitingResource when RAM/slots are short)
//     → JobEngine worker            (algorithm / provider adapter)
//     → terminal transition         (thread-safe completion callbacks)
//     → ExecutionHandle/Result      (exactly-once, event-loop-independent)
//     → OutputCommitter             (temp → stable asset, commit exactly once)
//     → DataManager / provenance    (asset id, lineage, taskReference)
//
// Design rules (issues #559 / ADR 0051 lineage):
//   * Sync waiting NEVER depends on Qt event-loop delivery. Terminal
//     notifications arrive through TaskCenter completion callbacks invoked on
//     the transitioning thread; a blocked waiter wakes on a condition
//     variable, so a caller on the payload/bridge thread cannot deadlock.
//   * Async completion delivery marshals onto an affinity QObject's thread
//     via QueuedConnection — safe because async consumers pump their loops.
//   * Result completion is exactly-once per task, enforced by TaskCenter
//     terminal deduplication plus the plane's commit-once guard.
#pragma once

#include <json/json.h>

#include <QDateTime>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "framework/task_center.h"

class QObject;

namespace sicnu::processing {

/// Lifecycle of one execution as observed through the plane. Mirrors
/// TaskStatus plus the sync-waiter-only TimedOut outcome (the underlying task
/// is canceled on timeout when the request allows it).
enum class ExecutionState
{
  Created,
  Submitted,        ///< accepted by TaskCenter, not yet admitted
  WaitingResource,  ///< held by resource admission (RAM budget / slots / RSS)
  Running,
  Cancelling,
  Completed,
  Failed,
  Canceled,
  TimedOut          ///< await() deadline hit; task cancel was requested
};

/// What to run and how: one entry-agnostic description of an execution.
struct ExecutionRequest
{
  QString algorithmId;              ///< canonical id ("rs:...", "gdal:...", "stub:...")
  QVariantMap params;               ///< typed parameters
  QString source = QStringLiteral( "plane" ); ///< entry tag: "agent", "mcp", "gui", "cli", "workflow"
  QString correlationId;            ///< entry-side correlation id (agent turn, JSON-RPC id, …)
  sicnu::TaskPriority priority = sicnu::TaskPriority::Normal;
  bool autoLoad = false;            ///< request layer auto-load after completion
  bool autoDispatch = true;         ///< TaskCenter-managed admission+dispatch
  /// Optional admission input (MiB). 0 → the TaskCenter estimate resolver
  /// (registry-declared estimate, conservative per-memory-class fallback).
  /// This is the field AlgorithmPreflight's `resources.estimatedRamBytes`
  /// feeds; see ExecutionPlane::estimateFromPreflight.
  unsigned int resourceEstimateMb = 0;
  QList<long> parentTaskIds;        ///< DAG gating (workflow pipelines)
  std::chrono::milliseconds timeout{ 0 }; ///< enforced by await/awaitResult
  bool cancelOnTimeout = true;      ///< request TaskCenter cancel when the deadline hits
};

/// Per-run context: identity + entry provenance for one execution. Value
/// object created by the plane; carried by handles and results so logging,
/// provenance and cancellation share one notion of "which run is this".
class ExecutionContext
{
  public:
    ExecutionContext() = default;

    QString runId() const { return m_runId; }
    QString source() const { return m_source; }
    QString correlationId() const { return m_correlationId; }
    long taskId() const { return m_taskId; }
    QDateTime submittedAt() const { return m_submittedAt; }

  private:
    friend class ExecutionPlane;
    QString m_runId;
    QString m_source;
    QString m_correlationId;
    long m_taskId = -1;
    QDateTime m_submittedAt;
};

/// Terminal outcome of one execution. `payload` is the standardized task
/// result (status / output / taskId / errorMessage); committed-output fields
/// are filled by the plane's commit-once payload builder (see
/// ExecutionPlane::buildCommittedResultPayload).
struct ExecutionResult
{
  ExecutionState state = ExecutionState::Created;
  long taskId = -1;
  QString algorithmId;
  Json::Value payload;
  QString committedOutputPath;
  QString commitError;
  QDateTime completedAtUtc;

  bool ok() const { return state == ExecutionState::Completed; }
  static QString stateName( ExecutionState state );
};

/// Async handle to a submitted execution. Cheap to copy; safe to destroy
/// before completion (the terminal callback is unregistered on last release).
/// await() blocks WITHOUT running or requiring any event loop — completion is
/// signalled through TaskCenter's thread-safe terminal callbacks.
class ExecutionHandle
{
  public:
    ExecutionHandle() = default;
    ~ExecutionHandle(); ///< deregisters the terminal callback on last release

    bool valid() const { return m_shared != nullptr; }
    long taskId() const { return m_shared ? m_shared->taskId : -1; }

    /// Current state snapshot (reads TaskCenter bookkeeping).
    ExecutionState state() const;

    /// Request cancellation; idempotent. Routes to TaskCenter::cancelTask
    /// (which cancels DAG descendants as well).
    void cancel() const;

    /// Block until terminal state, shutdown, or timeout. Returns true when a
    /// terminal state was reached (see result()/TaskCenter). Never requires an
    /// event loop on ANY thread — this is the #559 regression contract.
    bool await( std::chrono::milliseconds timeout ) const;

  private:
    friend class ExecutionPlane;
    struct Shared
    {
        long taskId = -1;
        std::mutex mutex;
        std::condition_variable cv;
        bool terminal = false;
        std::atomic<bool> cancelRequested{ false };
        // Completion-callback token for deregistration on last release. 0 when
        // the task was already terminal at registration (fired inline).
        long callbackToken = 0;
    };
    std::shared_ptr<Shared> m_shared;

    explicit ExecutionHandle( std::shared_ptr<Shared> shared )
      : m_shared( std::move( shared ) )
    {
    }
};

/// The unified execution facade. A process-wide singleton delegating to the
/// existing TaskCenter/JobEngine spine; owns no worker threads and no event
/// loop dependencies.
class ExecutionPlane
{
  public:
    /// Output committer handler signature (same contract as
    /// ToolCallDispatcher::OutputCommitterHandler): commit the task's temp
    /// output to a stable asset; returns the stable path or the error.
    using OutputCommitterHandler =
      std::function<bool( const sicnu::AlgorithmTaskInfo &, std::string &outCommittedPath, std::string &outCommitError )>;

    static ExecutionPlane &instance();

    /// Submit an execution; returns an async handle. Never blocks beyond
    /// TaskCenter enqueue work. The handle's completion channel is
    /// event-loop-independent (see ExecutionHandle::await).
    ExecutionHandle submit( const ExecutionRequest &request );

    /// Execution context for a submitted task (identity/provenance snapshot).
    ExecutionContext contextFor( long taskId ) const;

    /// Watch an externally-submitted task (e.g. via ToolCallDispatcher's
    /// sink): invokes @a onTerminal EXACTLY ONCE, on the thread performing the
    /// terminal transition, outside TaskCenter locks. When @a affinityContext
    /// is given, @a deliver runs on that QObject's thread (QueuedConnection)
    /// whenever the transitioning thread differs and a QCoreApplication exists
    /// — otherwise @a deliver runs inline. Returns false when the task is
    /// unknown. If the task is already terminal, @a deliver runs inline on the
    /// calling thread immediately.
    bool watch( long taskId,
                std::function<void( const sicnu::AlgorithmTaskInfo &)> deliver,
                QObject *affinityContext = nullptr ) const;

    /// Marshal @a fn onto @a affinityContext's thread when needed: queued when
    /// the current thread differs and an application object exists, inline
    /// otherwise. Async consumers' threads pump their event loops, so queued
    /// delivery cannot deadlock them (sync waiters use await(), not this).
    static void deliverOnAffinity( QObject *affinityContext, std::function<void()> fn );

    /// Build the standardized result payload for a terminal task, applying
    /// the committer handler EXACTLY ONCE per task id (first builder wins;
    /// later builders reuse the cached outcome). This is what allows several
    /// surfaces (dispatcher watcher, copilot signal handler, sync awaiter) to
    /// build payloads for the same task without racing the transactional
    /// commit or double-registering assets. Thread/affinity note: the actual
    /// commit runs on the CALLING thread — callers on the DataManager's
    /// owning thread (the normal case: bridge thread, sync waiter) are safe.
    Json::Value buildCommittedResultPayload( const sicnu::AlgorithmTaskInfo &info,
                                             const OutputCommitterHandler &committerHandler );

    /// Sync completion: wait (event-loop-free) for @a taskId, enforce the
    /// timeout (cancel on expiry when the request allows), then build the
    /// committed payload on the CALLING thread (affinity-correct for the
    /// standard case where the caller owns the DataManager). @a affinityContext
    /// is only used to marshal the payload build when the caller is NOT the
    /// affinity thread and an event loop exists there.
    Json::Value awaitResult( long taskId,
                             std::chrono::milliseconds timeout,
                             const OutputCommitterHandler &committerHandler,
                             QObject *affinityContext = nullptr,
                             bool cancelOnTimeout = true );

    /// Preflight → Admission bridge: run AlgorithmPreflight for @a
    /// algorithmId/@a params and extract its resource estimate (MiB, 0 when
    /// unknown) for ExecutionRequest::resourceEstimateMb. Lightweight GDAL
    /// probing only — callers choose when to pay for it.
    static unsigned int estimateFromPreflight( const std::string &algorithmId, const Json::Value &params );

    /// Point-in-time admission snapshot passthrough (see TaskCenter).
    sicnu::TaskAdmissionSnapshot admissionSnapshot( const QString &algorithmId,
                                                    unsigned int resourceEstimateMb = 0 ) const;

  private:
    ExecutionPlane() = default;
    ~ExecutionPlane() = default;
    ExecutionPlane( const ExecutionPlane & ) = delete;
    ExecutionPlane &operator=( const ExecutionPlane & ) = delete;

    friend class ExecutionHandle;

    static ExecutionState stateForTaskStatus( sicnu::TaskStatus status );

    struct CommitOutcome
    {
        Json::Value payload;
        QDateTime at;
    };
    /// Commit-once cache: taskId → payload built with commit applied. Bounded:
    /// oldest entries are evicted beyond kMaxCommitCacheEntries.
    mutable std::mutex m_commitMutex;
    std::map<long, CommitOutcome> m_commitCache;
    static constexpr std::size_t kMaxCommitCacheEntries = 256;
};

} // namespace sicnu::processing

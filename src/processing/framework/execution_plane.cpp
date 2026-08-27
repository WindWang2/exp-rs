// src/processing/framework/execution_plane.cpp
#include "execution_plane.h"

#include "algorithm_preflight.h"
#include "tool_call_dispatcher.h"

#include <QCoreApplication>
#include <QThread>

#include <algorithm>
#include <future>

namespace sicnu::processing {

namespace {

/// Await slices: completion itself is signalled by a condition variable, but
/// shutdown is a flag — slice the wait so an awaiter notices shutdown within
/// one slice instead of sleeping to its full timeout.
constexpr auto kAwaitSlice = std::chrono::milliseconds( 25 );

} // namespace

QString ExecutionResult::stateName( ExecutionState state )
{
  switch ( state )
  {
    case ExecutionState::Created: return QStringLiteral( "created" );
    case ExecutionState::Submitted: return QStringLiteral( "submitted" );
    case ExecutionState::WaitingResource: return QStringLiteral( "waiting_resource" );
    case ExecutionState::Running: return QStringLiteral( "running" );
    case ExecutionState::Cancelling: return QStringLiteral( "cancelling" );
    case ExecutionState::Completed: return QStringLiteral( "completed" );
    case ExecutionState::Failed: return QStringLiteral( "failed" );
    case ExecutionState::Canceled: return QStringLiteral( "canceled" );
    case ExecutionState::TimedOut: return QStringLiteral( "timed_out" );
  }
  return QStringLiteral( "unknown" );
}

ExecutionPlane &ExecutionPlane::instance()
{
  static ExecutionPlane s_instance;
  return s_instance;
}

ExecutionState ExecutionPlane::stateForTaskStatus( sicnu::TaskStatus status )
{
  switch ( status )
  {
    case sicnu::TaskStatus::Queued: return ExecutionState::Submitted;
    case sicnu::TaskStatus::WaitingResource: return ExecutionState::WaitingResource;
    case sicnu::TaskStatus::Running: return ExecutionState::Running;
    case sicnu::TaskStatus::Cancelling: return ExecutionState::Cancelling;
    case sicnu::TaskStatus::Paused: return ExecutionState::Running;
    case sicnu::TaskStatus::Completed: return ExecutionState::Completed;
    case sicnu::TaskStatus::Failed: return ExecutionState::Failed;
    case sicnu::TaskStatus::Canceled: return ExecutionState::Canceled;
  }
  return ExecutionState::Submitted;
}

ExecutionHandle ExecutionPlane::submit( const ExecutionRequest &request )
{
  auto &center = sicnu::TaskCenter::instance();
  const long taskId = center.enqueueTask( request.algorithmId, request.params, request.autoLoad,
                                          request.priority, request.parentTaskIds, request.autoDispatch,
                                          request.resourceEstimateMb, request.source );

  auto shared = std::make_shared<ExecutionHandle::Shared>();
  shared->taskId = taskId;
  if ( taskId > 0 )
  {
    // Event-loop-independent terminal channel: the callback fires on the
    // thread performing the terminal transition (usually a JobEngine worker)
    // and only flips a mutex-guarded flag — no Qt delivery in the wakeup path.
    std::weak_ptr<ExecutionHandle::Shared> weak = shared;
    shared->callbackToken = center.addTaskCompletionCallback(
      taskId, [weak]( const sicnu::AlgorithmTaskInfo & ) {
        if ( auto s = weak.lock() )
        {
          {
            std::lock_guard<std::mutex> lock( s->mutex );
            s->terminal = true;
          }
          s->cv.notify_all();
        }
      } );
  }

  return ExecutionHandle( std::move( shared ) );
}

ExecutionContext ExecutionPlane::contextFor( long taskId ) const
{
  ExecutionContext ctx;
  const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
  ctx.m_taskId = taskId;
  ctx.m_source = info.source;
  ctx.m_submittedAt = info.startTime;
  ctx.m_runId = QStringLiteral( "run-%1" ).arg( taskId );
  return ctx;
}

bool ExecutionPlane::watch( long taskId,
                            std::function<void( const sicnu::AlgorithmTaskInfo & )> deliver,
                            QObject *affinityContext ) const
{
  if ( taskId <= 0 || !deliver )
    return false;

  auto dispatch = [deliver, affinityContext]( const sicnu::AlgorithmTaskInfo &info ) {
    deliverOnAffinity( affinityContext, [info, deliver]() { deliver( info ); } );
  };

  // addTaskCompletionCallback returns >0 when registered, and 0 both for an
  // unknown task and for an already-terminal task where it fired the callback
  // inline — distinguish by task existence.
  if ( sicnu::TaskCenter::instance().addTaskCompletionCallback( taskId, std::move( dispatch ) ) > 0 )
    return true;
  return sicnu::TaskCenter::instance().getTaskInfo( taskId ).taskId == taskId;
}

void ExecutionPlane::deliverOnAffinity( QObject *affinityContext, std::function<void()> fn )
{
  if ( !fn )
    return;
  if ( affinityContext && QCoreApplication::instance()
       && affinityContext->thread() != QThread::currentThread() )
  {
    QMetaObject::invokeMethod( affinityContext, [fn = std::move( fn )]() { fn(); },
                               Qt::QueuedConnection );
  }
  else
  {
    fn();
  }
}

Json::Value ExecutionPlane::buildCommittedResultPayload( const sicnu::AlgorithmTaskInfo &info,
                                                         const OutputCommitterHandler &committerHandler )
{
  if ( info.taskId <= 0 )
    return ToolCallDispatcher::buildTaskResultPayload( info, committerHandler );

  // One builder at a time globally: commits are rare (terminal only) and this
  // closes the commit race (watcher vs signal-handler vs sync waiter) without
  // per-task locks. The transactional commit therefore runs exactly once per
  // task id; later builders reuse the winner's payload.
  std::lock_guard<std::mutex> buildLock( m_commitMutex );
  auto it = m_commitCache.find( info.taskId );
  if ( it != m_commitCache.end() )
    return it->second.payload;

  Json::Value payload = ToolCallDispatcher::buildTaskResultPayload( info, committerHandler );

  CommitOutcome outcome;
  outcome.payload = payload;
  outcome.at = QDateTime::currentDateTimeUtc();
  if ( m_commitCache.size() >= kMaxCommitCacheEntries )
  {
    // Evict the oldest outcome; task ids never repeat, so a stale entry only
    // matters for pathological late double-build attempts.
    auto oldest = std::min_element( m_commitCache.begin(), m_commitCache.end(),
                                    []( const auto &a, const auto &b ) { return a.second.at < b.second.at; } );
    if ( oldest != m_commitCache.end() )
      m_commitCache.erase( oldest );
  }
  m_commitCache[info.taskId] = std::move( outcome );
  return payload;
}

Json::Value ExecutionPlane::awaitResult( long taskId,
                                         std::chrono::milliseconds timeout,
                                         const OutputCommitterHandler &committerHandler,
                                         QObject *affinityContext,
                                         bool cancelOnTimeout )
{
  auto &center = sicnu::TaskCenter::instance();

  {
    // Unknown task (never submitted / pruned): fail fast instead of idling
    // to the timeout.
    const auto probe = center.getTaskInfo( taskId );
    if ( probe.taskId != taskId )
    {
      Json::Value errorResult( Json::objectValue );
      errorResult["status"] = "error";
      errorResult["taskId"] = static_cast<Json::Int64>( taskId );
      errorResult["errorMessage"] = "Unknown task id";
      return errorResult;
    }
  }

  auto shared = std::make_shared<ExecutionHandle::Shared>();
  shared->taskId = taskId;
  shared->callbackToken = center.addTaskCompletionCallback(
    taskId, [weak = std::weak_ptr<ExecutionHandle::Shared>( shared )]( const sicnu::AlgorithmTaskInfo & ) {
      if ( auto s = weak.lock() )
      {
        {
          std::lock_guard<std::mutex> lock( s->mutex );
          s->terminal = true;
        }
        s->cv.notify_all();
      }
    } );

  bool terminal = false;
  {
    std::unique_lock<std::mutex> lock( shared->mutex );
    if ( shared->terminal )
    {
      terminal = true; // task was already terminal; callback fired inline
    }
    else
    {
      const auto deadline = std::chrono::steady_clock::now() + timeout;
      while ( !shared->terminal && !center.isShuttingDown() )
      {
        const auto now = std::chrono::steady_clock::now();
        if ( now >= deadline )
          break;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>( deadline - now );
        shared->cv.wait_for( lock, std::min( remaining, kAwaitSlice ) );
      }
      terminal = shared->terminal;
    }
  }

  if ( !terminal )
  {
    // Deadline or shutdown. Request cancellation so a late worker result does
    // not silently keep resources and produce an unclaimed output; the late
    // terminal transition then lands in the normal exactly-once path. If the
    // task finishes while cancel is in flight, its real payload is returned.
    if ( cancelOnTimeout && !center.isShuttingDown() )
      center.cancelTask( taskId );

    // Cooperative-cancel grace: bounded wait for the Canceled/Completed record
    // so the returned payload reflects the real outcome instead of a guess.
    {
      const auto graceDeadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 2000 );
      std::unique_lock<std::mutex> lock( shared->mutex );
      while ( !shared->terminal && !center.isShuttingDown()
              && std::chrono::steady_clock::now() < graceDeadline )
      {
        shared->cv.wait_for( lock, kAwaitSlice );
      }
      terminal = shared->terminal;
    }

    if ( !terminal )
    {
      Json::Value errorResult( Json::objectValue );
      errorResult["status"] = "error";
      errorResult["taskId"] = static_cast<Json::Int64>( taskId );
      errorResult["errorMessage"] =
        center.isShuttingDown() ? "Execution aborted: application shutdown." : "Tool call timed out";
      return errorResult;
    }
  }

  const sicnu::AlgorithmTaskInfo info = center.getTaskInfo( taskId );

  // Payload construction (which commits through the Data Manager) must run on
  // the affinity thread when the caller is not it. The affinity thread is NOT
  // the blocked one (this thread is), so queued delivery cannot deadlock. The
  // promise is shared_ptr-owned so a delivery that outlives the bounded wait
  // below cannot touch a destroyed promise.
  if ( committerHandler && affinityContext && QCoreApplication::instance()
       && affinityContext->thread() != QThread::currentThread() )
  {
    auto promise = std::make_shared<std::promise<Json::Value>>();
    auto future = promise->get_future();
    QMetaObject::invokeMethod(
      affinityContext,
      [this, info, committerHandler, promise]() {
        promise->set_value( buildCommittedResultPayload( info, committerHandler ) );
      },
      Qt::QueuedConnection );
    if ( future.wait_for( std::chrono::milliseconds( 5000 ) ) == std::future_status::ready )
      return future.get();
    // Affinity thread starved (no pumping): fall through to a raw payload so
    // the caller still gets a result. No commit runs — safer than a wrong-thread commit.
    return ToolCallDispatcher::buildTaskResultPayload( info, nullptr );
  }

  return buildCommittedResultPayload( info, committerHandler );
}

unsigned int ExecutionPlane::estimateFromPreflight( const std::string &algorithmId, const Json::Value &params )
{
  const Json::Value report = preflightAlgorithm( algorithmId, params );
  if ( !report.isObject() )
    return 0;
  const Json::Value &resources = report["resources"];
  if ( resources.isObject() && resources.isMember( "estimatedRamBytes" ) )
  {
    const Json::UInt64 bytes = resources["estimatedRamBytes"].asUInt64();
    return static_cast<unsigned int>( bytes / ( 1024ull * 1024ull ) );
  }
  return 0;
}

sicnu::TaskAdmissionSnapshot ExecutionPlane::admissionSnapshot( const QString &algorithmId,
                                                                unsigned int resourceEstimateMb ) const
{
  return sicnu::TaskCenter::instance().admissionSnapshot( algorithmId, resourceEstimateMb );
}

// ---------------------------------------------------------------------------
// ExecutionHandle
// ---------------------------------------------------------------------------

ExecutionState ExecutionHandle::state() const
{
  if ( !m_shared )
    return ExecutionState::Created;
  return ExecutionPlane::stateForTaskStatus(
    sicnu::TaskCenter::instance().getTaskInfo( m_shared->taskId ).status );
}

void ExecutionHandle::cancel() const
{
  if ( !m_shared )
    return;
  bool expected = false;
  if ( !m_shared->cancelRequested.compare_exchange_strong( expected, true ) )
    return; // idempotent: only the first cancel call routes through
  sicnu::TaskCenter::instance().cancelTask( m_shared->taskId );
}

bool ExecutionHandle::await( std::chrono::milliseconds timeout ) const
{
  if ( !m_shared )
    return false;
  auto &center = sicnu::TaskCenter::instance();
  std::unique_lock<std::mutex> lock( m_shared->mutex );
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while ( !m_shared->terminal && !center.isShuttingDown() )
  {
    const auto now = std::chrono::steady_clock::now();
    if ( now >= deadline )
      break;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>( deadline - now );
    m_shared->cv.wait_for( lock, std::min( remaining, kAwaitSlice ) );
  }
  return m_shared->terminal;
}

ExecutionHandle::~ExecutionHandle()
{
  if ( !m_shared )
    return;
  // The shared state may outlive this handle via copies; only the last release
  // deregisters. TaskCenter is a process-lifetime singleton whose callback map
  // outlives handles in practice, and callbacks that already fired were erased
  // by fireTaskCompletionCallbacks — so this removal is always well-formed.
  if ( m_shared.use_count() == 1 && m_shared->callbackToken > 0 && m_shared->taskId > 0 )
  {
    sicnu::TaskCenter::instance().removeTaskCompletionCallback( m_shared->taskId, m_shared->callbackToken );
  }
}

} // namespace sicnu::processing

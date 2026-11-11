// JobEngine implementation
#include "job_engine.h"

#include "runtime/observability/trace.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSemaphore>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <utility>

namespace sicnu::jobs {

namespace {

JobLogLevel logLevelFromString( const std::string &level )
{
  if ( level == "warning" || level == "Warning" )
    return JobLogLevel::Warning;
  if ( level == "error" || level == "Error" )
    return JobLogLevel::Error;
  return JobLogLevel::Info;
}

bool isTerminalState( JobState state )
{
  return state == JobState::Succeeded || state == JobState::Failed || state == JobState::Cancelled;
}

/// Runs @p body on the Qt application thread (R29-T02 WP09 / H1): the queued
/// invocation is serviced by the host's event loop while this worker parks on
/// a semaphore — deliberately NOT BlockingQueuedConnection, because a GUI
/// thread parked in TaskCenter::waitForTask does not process events and would
/// deadlock that caller (waitForTask now services the event loop while it
/// waits, so the lane is reachable there too). Cancellation cooperates: a
/// cancel armed before the call starts turns the queued body into a no-op and
/// the wait returns as Cancelled; a cancel armed mid-run surfaces through the
/// operator's own cooperative checks. Exceptions are rethrown on the worker
/// so the engine's normal finish* paths stay untouched.
///
/// All shared state lives in a shared_ptr holder captured BY VALUE by the
/// queued functor (R1 review P1-3): if the worker abandons the wait after
/// the cancel-grace deadline and returns, a late-serviced invocation sees
/// `abandoned` and no-ops — it must never touch a destroyed stack frame, and
/// a job the engine already recorded terminal must not re-run the operator
/// for side effects hours later.
///
/// Un-abandoned waits are unbounded when no cancel is armed: a healthy host
/// thread services the event loop (that is the lane's premise), and a job
/// wall-clock budget (#1767 timeoutMs) still bounds the pathological case
/// through the ordinary cancel path.
Json::Value runOnHostQtThread(
    std::function<Json::Value()> body,
    const std::shared_ptr<std::atomic<bool>> &cancelFlag )
{
  struct HostRunState
  {
    QSemaphore completed;
    std::exception_ptr error;
    Json::Value result;
    bool ran = false;
    std::atomic<bool> abandoned{ false };
  };
  const auto state = std::make_shared<HostRunState>();
  const bool queued = QMetaObject::invokeMethod(
      QCoreApplication::instance(),
      [state, body, cancelFlag]() {
        if ( state->abandoned.load( std::memory_order_acquire ) )
          return; // worker gave up long ago — no side effects, no touch
        if ( cancelFlag && cancelFlag->load( std::memory_order_acquire ) )
        {
          state->completed.release();
          return;
        }
        try
        {
          state->result = body();
          state->ran = true;
        }
        catch ( ... )
        {
          state->error = std::current_exception();
        }
        state->completed.release();
      },
      Qt::QueuedConnection );
  if ( !queued )
    throw sicnu::operators::RSOperatorError(
        sicnu::operators::ErrorCode::NotInitialized,
        "job engine: cannot queue host-thread execution (no Qt application "
        "object)" );

  while ( !state->completed.tryAcquire( 1, 50 ) )
  {
    if ( cancelFlag && cancelFlag->load( std::memory_order_acquire ) )
    {
      // Bounded cancel grace: a still-pending queued call observes the flag
      // and no-ops; a RUNNING call finishes cooperatively through the
      // operator's own checks and completes normally (a result produced
      // despite cancellation is honored by finishSuccess's late-cancel
      // semantics, #1803). No completion after the grace means the host
      // thread is not servicing its event loop — fail closed as Cancelled
      // (never hang), and mark the invocation abandoned so a late delivery
      // cannot re-run the operator.
      if ( state->completed.tryAcquire( 1, 5000 ) )
        break;
      state->abandoned.store( true, std::memory_order_release );
      throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled,
          "job engine: the Qt host thread did not service host-thread "
          "execution within the cancel grace window (host blocked without an "
          "event loop?)" );
    }
  }
  if ( state->error )
    std::rethrow_exception( state->error );
  if ( !state->ran )
    throw sicnu::operators::RSOperatorError(
        sicnu::operators::ErrorCode::Cancelled,
        "Operator execution was cancelled" );
  return state->result;
}

/// #798: true while running on a JobEngine worker thread. Worker-originated
/// submit() calls raise transient pool capacity so a body that blocks on a
/// sub-job can never starve the sub-job's pick.
thread_local bool t_isWorkerThread = false;
thread_local std::string t_currentJobId;

} // namespace

bool JobEngine::isWorkerThread()
{
  return t_isWorkerThread;
}

const std::string &JobEngine::currentJobId()
{
  return t_currentJobId;
}

JobEngine &JobEngine::instance()
{
  // Immortal singleton (#1767): workers are spawned DETACHED and a stuck
  // worker may still be unwinding after shutdown() returned — the engine
  // object must outlive every worker, so it is heap-allocated and never
  // destroyed (process exit reclaims it). A function-local static OBJECT
  // would run its destructor at exit while a detached worker still touches
  // it (use-after-destroy).
  static JobEngine *e = new JobEngine();
  return *e;
}

int JobEngine::defaultWorkerCount( unsigned hardwareConcurrency )
{
  // One worker per core minus the UI-reserved core (ADR 0002), floored at
  // kMinWorkers. hardwareConcurrency == 0 means "unknown" — degrade to
  // the floor instead of an empty pool.
  const int cores = hardwareConcurrency > 0 ? static_cast<int>( hardwareConcurrency )
                                            : kMinWorkers;
  return std::max( kMinWorkers, cores - 1 );
}

int JobEngine::defaultWorkerCount()
{
  const unsigned hw = std::thread::hardware_concurrency();
  return defaultWorkerCount( hw );
}

JobEngine::JobEngine() = default;

JobEngine::~JobEngine()
{
  shutdown();
}

std::size_t JobEngine::shutdown()
{
  std::vector<std::thread> toDrop;
  std::vector<JobRecord> cancelledRecords;
  std::vector<std::function<void()>> cancelHooks;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    if ( m_shuttingDown || m_terminated )
      return 0;
    m_shuttingDown = true;
    m_terminated = true; // sticky (#684): submit() must never respawn workers
    m_stop.store( true );
    m_generation++;
    toDrop.swap( m_workers );
    // #1767: no per-job deadlines survive shutdown — the watchdog has
    // nothing left to guard.
    m_jobDeadlines.clear();

    // Arm cancel flags of running jobs and collect cancel hooks. Also cover
    // the tryPick→runOperatorJob window: a job popped as Running whose flag
    // has not been created/adopted yet gets a pre-armed flag that
    // runOperatorJob adopts (review P2 — cancel() handles this, shutdown
    // didn't).
    for ( auto &kv : m_cancelFlags )
    {
      if ( kv.second )
        kv.second->store( true, std::memory_order_release );
    }
    for ( auto &kv : m_jobs )
    {
      if ( kv.second.state != JobState::Running )
        continue;
      auto fit = m_cancelFlags.find( kv.first );
      if ( fit == m_cancelFlags.end() || !fit->second )
        m_cancelFlags[kv.first] = std::make_shared<std::atomic<bool>>( true );
    }
    for ( auto &kv : m_jobBodies )
    {
      if ( kv.second.onCancel )
      {
        cancelHooks.push_back( std::move( kv.second.onCancel ) );
        kv.second.onCancel = nullptr;
      }
    }

    while ( m_queuedCount > 0 )
    {
      // Drain in (priority, FIFO) order, exclusives last by their submission
      // FIFO. Order among Cancelled notifications is not contractual — every
      // consumer dedups terminal transitions.
      std::string qId;
      if ( !m_exclusiveQueue.empty() )
      {
        qId = m_exclusiveQueue.front();
        m_exclusiveQueue.pop_front();
      }
      else if ( !m_queueBuckets.empty() )
      {
        auto bucket = m_queueBuckets.begin();
        qId = bucket->second.front();
        bucket->second.pop_front();
        if ( bucket->second.empty() )
          m_queueBuckets.erase( bucket );
      }
      else
      {
        // m_queuedCount desynced from the buckets (cannot happen — every
        // mutation site is symmetric); stop instead of dereferencing end().
        m_queuedCount = 0;
        break;
      }
      m_queuedCount -= 1;
      auto it = m_jobs.find( qId );
      if ( it != m_jobs.end() && it->second.state == JobState::Queued )
      {
        it->second.state = JobState::Cancelled;
        it->second.finishedAtMs = nowUnixMs();
        it->second.statusMessage = "Cancelled due to shutdown";
        appendLog( it->second, JobLogLevel::Info, "Cancelled due to shutdown" );
        m_jobBodies.erase( qId );
        m_cancelFlags.erase( qId );
        cancelledRecords.push_back( it->second );
      }
    }
  }
  m_cv.notify_all();

  // Invoked WITHOUT m_mutex held so cancel hooks can re-enter JobEngine without deadlocking
  for ( auto &hook : cancelHooks )
  {
    if ( hook )
    {
      try { hook(); } catch ( ... ) {}
    }
  }

  for ( const auto &rec : cancelledRecords )
    notify( rec );

  // Bounded worker-exit wait (#1767 / RIO-8): a worker parked in a
  // non-cancellable operator cannot be interrupted — after the deadline it
  // is abandoned (already detached at spawn; it unwinds against this
  // immortal engine, can never pick another job via the generation/stop
  // gates, and its exit bookkeeping needs no join). Returns the stuck count.
  const std::size_t stuck = waitWorkersExitedBounded( m_shutdownJoinTimeoutMs );
  if ( stuck > 0 )
  {
    m_abandonedWorkers.store( true, std::memory_order_release );
    // WP02 observability: abandoned workers must be COUNTED, not silently
    // dropped — a shutdown that "completed" while a worker is still parked
    // in a native call reads as clean unless the number is queryable. The
    // return value covers in-process callers; this trace record makes the
    // event durable for post-mortem diagnostics.
    if ( sicnu::runtime::observability::trace::Trace::enabled() )
    {
      sicnu::runtime::observability::trace::TraceEvent abandoned;
      abandoned.op = "job_engine";
      abandoned.event = "shutdown_abandoned_workers";
      abandoned.phase = "end";
      abandoned.detail = "stuck=" + std::to_string( stuck );
      sicnu::runtime::observability::trace::Trace::publish( abandoned );
    }
  }
  // The thread objects were never joined (spawn-time detach): destroying
  // them is a no-op on detached threads.
  toDrop.clear();

  {
    std::lock_guard<std::mutex> lock( m_mutex );
    m_shuttingDown = false;
  }
  return stuck;
}

std::size_t JobEngine::waitWorkersExitedBounded( int timeoutMs )
{
  if ( timeoutMs < 0 )
    timeoutMs = 0;
  std::unique_lock<std::mutex> lock( m_mutex );
  const bool allExited = m_cv.wait_for(
      lock, std::chrono::milliseconds( timeoutMs ),
      [this] { return m_liveWorkers.load( std::memory_order_acquire ) == 0; } );
  return allExited ? std::size_t( 0 )
                   : static_cast<std::size_t>(
                         m_liveWorkers.load( std::memory_order_acquire ) );
}

void JobEngine::setShutdownJoinTimeoutMs( int ms )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_shutdownJoinTimeoutMs = ms < 0 ? 0 : ms;
}

int JobEngine::shutdownJoinTimeoutMs() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_shutdownJoinTimeoutMs;
}

void JobEngine::setMaxWorkers( int n )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_maxWorkers = std::clamp( n, kMinWorkers, kMaxWorkersOverride );
  if ( !m_stop.load() )
    ensureWorkersLocked();
  m_cv.notify_all();
}

int JobEngine::maxWorkers() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_maxWorkers;
}

void JobEngine::registerExecutor( const std::string &prefix, JobExecutor executor )
{
  if ( prefix.empty() || !executor )
    return;
  std::lock_guard<std::mutex> lock( m_mutex );
  // Replace existing same prefix; otherwise append (longer prefixes preferred at lookup).
  for ( auto &pair : m_prefixExecutors )
  {
    if ( pair.first == prefix )
    {
      pair.second = std::move( executor );
      return;
    }
  }
  m_prefixExecutors.emplace_back( prefix, std::move( executor ) );
  // Longest prefix first for stable matching.
  std::sort( m_prefixExecutors.begin(), m_prefixExecutors.end(),
             []( const auto &a, const auto &b ) { return a.first.size() > b.first.size(); } );
}

void JobEngine::clearExecutors()
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_prefixExecutors.clear();
}

void JobEngine::setFallbackExecutor( JobExecutor executor )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_fallbackExecutor = std::move( executor );
}

JobEngine::JobExecutor JobEngine::findPrefixExecutorLocked( const std::string &algorithmId ) const
{
  for ( const auto &pair : m_prefixExecutors )
  {
    if ( algorithmId.rfind( pair.first, 0 ) == 0 ) // starts_with
      return pair.second;
  }
  return {};
}

std::string JobEngine::submit( JobRequest req )
{
  return submitWithId( std::move( req ), std::string() );
}

std::string JobEngine::submit( JobRequest req, JobExecutor executor, CancelHook onCancel )
{
  return submitWithId( std::move( req ), std::string(), std::move( executor ), std::move( onCancel ) );
}

std::string JobEngine::submitWithId( JobRequest req, const std::string &requestedId,
                                     JobExecutor executor, CancelHook onCancel )
{
  std::string id;
  JobRecord copy;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    // #799: a caller-chosen id that is already owned by a live record is
    // refused — the engine creates nothing and the caller rolls back its
    // pre-registration.
    if ( !requestedId.empty() && m_jobs.count( requestedId ) > 0 )
      return std::string();
    id = !requestedId.empty() ? requestedId : "job-" + std::to_string( m_nextId.fetch_add( 1 ) );
    if ( m_shuttingDown || m_terminated )
    {
      JobRecord rec;
      rec.id = id;
      rec.request = std::move( req );
      rec.state = JobState::Cancelled;
      rec.createdAtMs = nowUnixMs();
      rec.finishedAtMs = rec.createdAtMs;
      rec.statusMessage = m_terminated ? "Cancelled: JobEngine is terminated"
                                       : "Cancelled: JobEngine is shutting down";
      m_jobs.emplace( id, rec );
      copy = rec;
    }
    else
    {
      JobRecord rec;
      rec.id = id;
      rec.request = std::move( req );
      rec.state = JobState::Queued;
      rec.createdAtMs = nowUnixMs();

      m_jobs.emplace( id, rec );
      if ( executor )
      {
        JobBody body;
        body.executor = std::move( executor );
        body.onCancel = std::move( onCancel );
        m_jobBodies.emplace( id, std::move( body ) );
      }
      enqueueJobLocked( id, rec.request );

      // #798: a worker-originated submit means a job body is about to wait
      // for this sub-job. Raise transient capacity (bounded) and respawn so a
      // saturated pool can always still pick the sub-job — otherwise every
      // worker could block on a queue that no thread can drain.
      if ( t_isWorkerThread && m_transientAllowance < kMaxTransientWorkers )
      {
        m_transientAllowance += 1;
        m_transientBacked.insert( id );
      }
      ensureWorkersLocked();
      copy = m_jobs.at( id );
    }
  }
  m_cv.notify_all();
  notify( copy );
  return id;
}

bool JobEngine::cancel( const std::string &jobId )
{
  JobRecord copy;
  bool changed = false;
  CancelHook cancelHook;
  bool cancelRunning = false;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    auto it = m_jobs.find( jobId );
    if ( it == m_jobs.end() )
      return false;

    JobRecord &rec = it->second;
    switch ( rec.state )
    {
      case JobState::Succeeded:
      case JobState::Failed:
      case JobState::Cancelled:
        return false;
      case JobState::Queued:
      {
        dequeueJobLocked( jobId, rec.request );
        rec.state = JobState::Cancelled;
        rec.finishedAtMs = nowUnixMs();
        rec.statusMessage = "Cancelled";
        appendLog( rec, JobLogLevel::Info, "Cancelled while queued" );
        auto bit = m_jobBodies.find( jobId );
        if ( bit != m_jobBodies.end() && bit->second.onCancel )
        {
          cancelHook = std::move( bit->second.onCancel );
        }
        m_jobBodies.erase( jobId );
        m_cancelFlags.erase( jobId );
        // #798: a cancelled transient-backed job must release its +1 capacity
        // slot exactly like a finishing one, or the allowance leaks and the
        // pick ceiling stays permanently raised (review L P2).
        if ( m_transientBacked.erase( jobId ) > 0 && m_transientAllowance > 0 )
          m_transientAllowance -= 1;
        copy = rec;
        changed = true;
        break;
      }
      case JobState::Running:
      {
        auto fit = m_cancelFlags.find( jobId );
        if ( fit == m_cancelFlags.end() || !fit->second )
        {
          // Worker picked the job but has not armed the cancel flag yet
          // (window between tryPickJobLocked and runOperatorJob). Arm a
          // pre-set flag; runOperatorJob adopts it via setCancelFlag.
          m_cancelFlags[jobId] = std::make_shared<std::atomic<bool>>( true );
        }
        else
        {
          fit->second->store( true );
        }
        auto bit = m_jobBodies.find( jobId );
        if ( bit != m_jobBodies.end() && bit->second.onCancel )
        {
          cancelHook = std::move( bit->second.onCancel );
          bit->second.onCancel = nullptr;
        }
        cancelRunning = true;
        // Terminal state set when operator observes cancel / exits
        break;
      }
    }
  }

  // Invoked WITHOUT m_mutex held (mirrors notify()): hooks may re-enter
  // JobEngine (snapshot, cancel of siblings) without deadlocking.
  if ( cancelHook )
  {
    try
    {
      cancelHook();
    }
    catch ( ... )
    {
      // cancel hooks must not throw into engine
    }
  }

  if ( changed )
  {
    m_cv.notify_all();
    notify( copy );
  }
  return changed || cancelRunning;
}

std::optional<JobRecord> JobEngine::snapshot( const std::string &jobId ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  auto it = m_jobs.find( jobId );
  if ( it == m_jobs.end() )
    return std::nullopt;
  return it->second;
}

std::vector<JobRecord> JobEngine::list() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  std::vector<JobRecord> out;
  out.reserve( m_jobs.size() );
  for ( const auto &kv : m_jobs )
    out.push_back( kv.second );
  return out;
}

std::size_t JobEngine::pruneCompleted( std::size_t maxKeep )
{
  std::lock_guard<std::mutex> lock( m_mutex );

  // Terminal cleanup (finishJobLocked) already erases m_cancelFlags /
  // m_jobBodies entries, so pruning only needs to drop the m_jobs records.
  std::vector<std::string> terminal;
  terminal.reserve( m_jobs.size() );
  for ( const auto &kv : m_jobs )
  {
    if ( isTerminalState( kv.second.state ) )
      terminal.push_back( kv.first );
  }
  if ( terminal.size() <= maxKeep )
    return 0;

  // Oldest first by the record's own timestamps: finishedAtMs, then
  // createdAtMs, then id (unique, so the order is total).
  std::sort( terminal.begin(), terminal.end(), [this]( const std::string &a, const std::string &b ) {
    const JobRecord &ra = m_jobs.at( a );
    const JobRecord &rb = m_jobs.at( b );
    if ( ra.finishedAtMs != rb.finishedAtMs )
      return ra.finishedAtMs < rb.finishedAtMs;
    if ( ra.createdAtMs != rb.createdAtMs )
      return ra.createdAtMs < rb.createdAtMs;
    return a < b;
  } );

  const std::size_t toRemove = terminal.size() - maxKeep;
  for ( std::size_t i = 0; i < toRemove; ++i )
    m_jobs.erase( terminal[i] );
  return toRemove;
}

std::size_t JobEngine::removeCompleted( const std::vector<std::string> &jobIds )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  std::size_t removed = 0;
  for ( const auto &jobId : jobIds )
  {
    auto it = m_jobs.find( jobId );
    if ( it == m_jobs.end() || !isTerminalState( it->second.state ) )
      continue; // unknown, or queued/running — never pruned
    m_jobs.erase( it );
    ++removed;
  }
  return removed;
}

void JobEngine::clearCompleted()
{
  pruneCompleted( 0 );
}

void JobEngine::setListener( Listener listener )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_listener = std::move( listener );
}

bool JobEngine::waitForJob( const std::string &jobId, int timeoutMs )
{
  // Deadlock prevention (#798): worker threads must not synchronously wait
  // on jobs because doing so can exhaust the pool and deadlock sub-jobs.
  if ( isWorkerThread() )
  {
    return false;
  }

  std::unique_lock<std::mutex> lock( m_mutex );
  auto it = m_jobs.find( jobId );
  if ( it == m_jobs.end() )
    return false;

  auto isFinished = [this, &jobId]() {
    auto it = m_jobs.find( jobId );
    return it == m_jobs.end() ||
           it->second.state == JobState::Succeeded ||
           it->second.state == JobState::Failed ||
           it->second.state == JobState::Cancelled;
  };

  if ( isFinished() )
    return true;

  if ( timeoutMs < 0 )
  {
    m_cv.wait( lock, isFinished );
  }
  else
  {
    if ( !m_cv.wait_for( lock, std::chrono::milliseconds( timeoutMs ), isFinished ) )
      return false;
  }

  auto endIt = m_jobs.find( jobId );
  return endIt != m_jobs.end() && (
    endIt->second.state == JobState::Succeeded ||
    endIt->second.state == JobState::Failed ||
    endIt->second.state == JobState::Cancelled );
}

void JobEngine::waitUntilIdleForTests( int timeoutMs )
{
  // #798: blocking waits are forbidden on worker threads — a worker waiting
  // for pool idle can never satisfy itself and starves the pool.
  if ( isWorkerThread() )
    return;
  std::unique_lock<std::mutex> lock( m_mutex );
  const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds( timeoutMs );
  while ( !( m_queuedCount == 0 && m_running == 0 ) )
  {
    if ( m_cv.wait_until( lock, deadline ) == std::cv_status::timeout )
    {
      // Leave; tests assert on job state / will fail if still busy
      return;
    }
  }
}

void JobEngine::shutdownForTests()
{
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    m_stop.store( true );
    // R1-6: same generation fence as production shutdown — an abandoned
    // straggler waking after the reset must still exit, never pick jobs.
    m_generation++;
    // #1767: same bounded discipline as production shutdown — a stuck
    // worker must not hang the test binary either. Stragglers stay
    // generation-gated out of job picking, so reuse stays correct.
    m_jobDeadlines.clear();
  }
  m_cv.notify_all();

  std::vector<std::thread> toDrop;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    toDrop.swap( m_workers );
  }
  // Test jobs are cooperative by construction; the bound is generous but
  // finite (detached spawn makes an over-deadline worker non-fatal).
  waitWorkersExitedBounded( 30'000 );
  toDrop.clear();

  {
    std::lock_guard<std::mutex> lock( m_mutex );
    m_queueBuckets.clear();
    m_exclusiveQueue.clear();
    m_queuedCount = 0;
    m_jobs.clear();
    m_cancelFlags.clear();
    m_jobBodies.clear();
    m_prefixExecutors.clear();
    m_fallbackExecutor = nullptr;
    m_maxWorkers = defaultWorkerCount();
    m_running = 0;
    m_exclusiveRunning = false;
    m_transientAllowance = 0; // #798
    m_transientBacked.clear();
    m_listener = nullptr;
    m_shuttingDown = false;
    m_terminated = false; // explicit test-only reset: the engine is reusable
    m_stop.store( false );
  }
  m_abandonedWorkers.store( false, std::memory_order_release );
  m_cv.notify_all();
}

bool JobEngine::isTerminated() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_terminated;
}

void JobEngine::ensureWorkersLocked()
{
  if ( m_shuttingDown || m_terminated )
    return; // terminated engines never respawn workers (#684)
  const uint64_t gen = m_generation;
  // #798: the pool may temporarily exceed m_maxWorkers by the transient
  // allowance raised by worker-originated sub-job submits.
  const int allowed = m_maxWorkers + m_transientAllowance;
  while ( static_cast<int>( m_workers.size() ) < allowed )
  {
    // #1767: spawn DETACHED against the immortal singleton and count the
    // exit — shutdown() waits on m_liveWorkers with a deadline instead of
    // joining blindly, and a worker stuck past the deadline simply keeps
    // unwinding on its own.
    m_liveWorkers.fetch_add( 1, std::memory_order_relaxed );
    m_workers.emplace_back( [this, gen]
    {
      workerLoop( gen );
      m_liveWorkers.fetch_sub( 1, std::memory_order_release );
      // Serialize against a shutdown() waiter's predicate check (empty
      // critical section) so this notify can never land between "checked
      // liveWorkers != 0" and "blocked" — the exit is observed immediately,
      // not at the deadline.
      {
        std::lock_guard<std::mutex> lock( m_mutex );
      }
      m_cv.notify_all();
    } );
    m_workers.back().detach();
  }
}

void JobEngine::ensureWatchdogLocked()
{
  if ( m_watchdogStarted )
    return;
  m_watchdogStarted = true;
  m_watchdog = std::thread( [this] { watchdogLoop(); } );
  // Immortal engine (#1767): the watchdog never needs joining; park-and-
  // wake costs nothing when no job carries a deadline.
  m_watchdog.detach();
}

void JobEngine::watchdogLoop()
{
  while ( true )
  {
    std::string fire;
    {
      std::unique_lock<std::mutex> lock( m_mutex );
      if ( m_jobDeadlines.empty() )
      {
        // No budgets armed: park until the next arm/notify (spurious
        // wakeups are harmless re-checks).
        m_cv.wait( lock );
        continue;
      }
      auto it = m_jobDeadlines.begin();
      const int64_t earliest = it->second;
      // Deliberately wall-clock (JobRequest::timeoutMs is documented as a
      // wall-clock budget): a clock step shifts budgets, it never breaks
      // the cooperative-cancel contract (R1-11).
      const int64_t now = nowUnixMs();
      if ( now < earliest )
      {
        m_cv.wait_until( lock,
                         std::chrono::system_clock::time_point(
                             std::chrono::milliseconds( earliest ) ) );
        continue;
      }
      fire = it->first;
      m_jobDeadlines.erase( it );
    }
    // Fire OUTSIDE m_mutex: cancel() re-enters the engine and may invoke
    // the job's CancelHook (which is also run lock-free by contract).
    if ( !fire.empty() && cancel( fire ) )
    {
      // The log line records WHY the job now unwinds as Cancelled; the
      // operator still finishes the cancellation cooperatively.
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( fire );
      if ( it != m_jobs.end() )
        appendLog( it->second, JobLogLevel::Warning,
                   "Wall-clock budget elapsed; cancellation fired" );
    }
  }
}

void JobEngine::enqueueJobLocked( const std::string &jobId, const JobRequest &req )
{
  if ( req.exclusive )
    m_exclusiveQueue.push_back( jobId );
  else
    m_queueBuckets[req.priority].push_back( jobId );
  m_queuedCount += 1;
}

bool JobEngine::dequeueJobLocked( const std::string &jobId, const JobRequest &req )
{
  if ( req.exclusive )
  {
    auto it = std::find( m_exclusiveQueue.begin(), m_exclusiveQueue.end(), jobId );
    if ( it == m_exclusiveQueue.end() )
      return false;
    m_exclusiveQueue.erase( it );
  }
  else
  {
    auto bucket = m_queueBuckets.find( req.priority );
    if ( bucket == m_queueBuckets.end() )
      return false;
    auto &dq = bucket->second;
    auto it = std::find( dq.begin(), dq.end(), jobId );
    if ( it == dq.end() )
      return false;
    dq.erase( it );
    if ( dq.empty() )
      m_queueBuckets.erase( bucket );
  }
  m_queuedCount -= 1;
  return true;
}

void JobEngine::workerLoop( uint64_t gen )
{
  // #798: mark this thread as a worker so submit() can raise transient
  // capacity for worker-originated sub-jobs, and blocking waits can refuse.
  t_isWorkerThread = true;
  while ( true )
  {
    std::string jobId;
    {
      std::unique_lock<std::mutex> lock( m_mutex );
      for ( ;; )
      {
        if ( m_shuttingDown || gen != m_generation || m_stop.load() )
        {
          t_isWorkerThread = false;
          return;
        }
        auto picked = tryPickJobLocked();
        if ( picked.has_value() )
        {
          jobId = std::move( *picked );
          break;
        }
        m_cv.wait( lock );
        if ( m_shuttingDown || gen != m_generation || m_stop.load() )
        {
          t_isWorkerThread = false;
          return;
        }
      }
    }

    // M1 structured hierarchy: expose the executing job id to nested
    // submissions (TaskCenter resolves the owner task edge through it).
    // runOperatorJob catches all executor exceptions internally, so the
    // clearing below always runs on the normal worker path.
    t_currentJobId = jobId;
    runOperatorJob( jobId );
    t_currentJobId.clear();
    m_cv.notify_all();
  }
}

std::optional<std::string> JobEngine::tryPickJobLocked()
{
  // Exclusive policy: drain in-flight work, then run exclusive alone.
  // See class comment on JobEngine.
  if ( m_queuedCount == 0 || m_exclusiveRunning )
    return std::nullopt;

  if ( !m_exclusiveQueue.empty() )
  {
    if ( m_running > 0 )
      return std::nullopt; // drain before exclusive

    // Reviewed P3 hardening: a stale entry (unreachable today — cancel
    // mutates record+buckets in one critical section) is DROPPED and the
    // scan continues instead of stalling the exclusive queue behind a
    // zombie head.
    while ( !m_exclusiveQueue.empty() )
    {
      const std::string id = m_exclusiveQueue.front();
      m_exclusiveQueue.pop_front();
      m_queuedCount -= 1;
      auto jit = m_jobs.find( id );
      if ( jit == m_jobs.end() || jit->second.state != JobState::Queued )
        continue;
      jit->second.state = JobState::Running;
      jit->second.startedAtMs = nowUnixMs();
      m_running += 1;
      m_exclusiveRunning = true;
      return id;
    }
    // The queue held only stale ids: fall through to the non-exclusive scan.
  }

  if ( m_running >= m_maxWorkers + m_transientAllowance )
    return std::nullopt; // #798: saturation includes transient sub-job capacity

  // Priority-aware pick (#686): TaskCenter stages in priority order, but its
  // admission count can exceed the worker pool, so several jobs may sit in
  // this queue. Pick the lowest priority value (High first), FIFO within the
  // same priority, instead of blind front-pop — otherwise a late-submitted
  // High task runs behind everything staged before it. The priority buckets
  // (8.0) make this O(log P) instead of a full-queue scan per pick. Stale
  // entries are dropped and the scan advances (reviewed P3 hardening).
  while ( !m_queueBuckets.empty() )
  {
    auto bucket = m_queueBuckets.begin();
    auto &dq = bucket->second;
    const std::string id = dq.front();
    dq.pop_front();
    if ( dq.empty() )
      m_queueBuckets.erase( bucket );
    m_queuedCount -= 1;

    auto jit = m_jobs.find( id );
    if ( jit == m_jobs.end() || jit->second.state != JobState::Queued )
      continue;
    jit->second.state = JobState::Running;
    jit->second.startedAtMs = nowUnixMs();
    m_running += 1;
    if ( jit->second.request.exclusive )
      m_exclusiveRunning = true;
    return id;
  }
  return std::nullopt;
}

void JobEngine::appendLog( JobRecord &rec, JobLogLevel level, const std::string &text )
{
  // #1705: logLines used to grow without bound for the lifetime of a
  // running job (a chatty operator printed its way to OOM), and every
  // snapshot/terminal notify copied the whole vector under m_mutex. The
  // retained history is now bounded: single lines above kMaxJobLogLineBytes
  // are truncated (no single print can dominate the budget), and once the
  // line count crosses kMaxJobLogLines the OLDEST block is evicted down to
  // kJobLogKeepLines (block eviction amortizes the front-erase cost; the
  // count of dropped lines is preserved in rec.droppedLogLines). Callers
  // must hold m_mutex — every existing call site does.
  constexpr std::size_t kMaxJobLogLineBytes = 4 * 1024;
  JobLogLine line;
  line.unixMs = nowUnixMs();
  line.level = level;
  line.text = text;
  if ( line.text.size() > kMaxJobLogLineBytes )
  {
    std::size_t cut = kMaxJobLogLineBytes;
    // Do not split a UTF-8 sequence at the cut boundary.
    while ( cut > 0 && ( static_cast<unsigned char>( line.text[cut] ) & 0xC0 ) == 0x80 )
      --cut;
    const std::size_t droppedBytes = line.text.size() - cut;
    line.text.resize( cut );
    line.text += " …[truncated " + std::to_string( droppedBytes ) + " bytes]";
  }
  rec.logLines.push_back( std::move( line ) );
  if ( rec.logLines.size() > kMaxJobLogLines )
  {
    const std::size_t evict = rec.logLines.size() - kJobLogKeepLines;
    rec.logLines.erase( rec.logLines.begin(),
                        rec.logLines.begin() + static_cast<std::ptrdiff_t>( evict ) );
    rec.droppedLogLines += evict;
    // No cursor adjustment: delta offsets reference the ABSOLUTE appended-
    // line space (droppedLogLines + local index, see the log callback), which
    // eviction does not renumber — consumers keep a monotonic exactly-once
    // index even as old lines leave the retained window.
  }
}

void JobEngine::notify( const JobRecord &rec )
{
  Listener listener;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    listener = m_listener;
  }
  if ( listener )
  {
    try
    {
      listener( rec );
    }
    catch ( ... )
    {
      // Listeners must never throw into JobEngine
    }
  }
}

void JobEngine::finishJobLocked( JobRecord &rec, bool wasExclusive )
{
  rec.finishedAtMs = nowUnixMs();
  if ( m_running > 0 )
    m_running -= 1;
  if ( wasExclusive )
    m_exclusiveRunning = false;
  // #798: a transient-backed sub-job finished — release its +1 capacity slot.
  if ( m_transientBacked.erase( rec.id ) > 0 && m_transientAllowance > 0 )
    m_transientAllowance -= 1;
  m_cancelFlags.erase( rec.id );
  m_jobBodies.erase( rec.id );
  m_deltaLogCursor.erase( rec.id );
  m_jobDeadlines.erase( rec.id ); // #1767: terminal — the budget is spent
}

void JobEngine::runOperatorJob( const std::string &jobId )
{
  JobRequest request;
  std::shared_ptr<std::atomic<bool>> cancelFlag;
  bool wasExclusive = false;
  JobExecutor executor;
  JobExecutor fallback; // registry catch-all (ADR 0062)

  JobRecord startedCopy;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    auto it = m_jobs.find( jobId );
    if ( it == m_jobs.end() )
      return;

    JobRecord &rec = it->second;
    request = rec.request;
    wasExclusive = request.exclusive;
    // Adopt an already-armed cancel flag if cancel() raced ahead of the
    // worker (window between tryPickJobLocked and here); otherwise create a
    // fresh one for this run.
    auto flagIt = m_cancelFlags.find( jobId );
    if ( flagIt != m_cancelFlags.end() && flagIt->second )
      cancelFlag = flagIt->second;
    else
    {
      cancelFlag = std::make_shared<std::atomic<bool>>( false );
      m_cancelFlags[jobId] = cancelFlag;
    }
    appendLog( rec, JobLogLevel::Info, "Started" );
    startedCopy = rec;
    // #1767: arm the optional wall-clock budget exactly once, at the
    // Running transition — the watchdog fires the cancel path at deadline.
    if ( request.timeoutMs > 0 )
    {
      m_jobDeadlines[jobId] = nowUnixMs() + request.timeoutMs;
      ensureWatchdogLocked();
      m_cv.notify_all();
    }

    auto bit = m_jobBodies.find( jobId );
    if ( bit != m_jobBodies.end() && bit->second.executor )
      executor = bit->second.executor;
    else
      executor = findPrefixExecutorLocked( request.algorithmId );
    // Snapshot the catch-all so registry lookup happens off the engine lock.
    fallback = m_fallbackExecutor;
  }
  notify( startedCopy );

  // Unified trace (Verification 7.0): one execution_start / execution_end
  // pair per job. Gated on Trace::enabled() — zero work when tracing is off.
  // The RAII end-guard covers every return path below; it reads the terminal
  // state via snapshot() (engine mutex, worker thread — safe, never held
  // across user code).
  if ( sicnu::runtime::observability::trace::Trace::enabled() )
  {
    sicnu::runtime::observability::trace::TraceEvent start;
    start.job = jobId;
    start.op = request.algorithmId;
    start.event = "execution_start";
    start.phase = "start";
    sicnu::runtime::observability::trace::Trace::publish( start );
  }
  const auto traceStart = std::chrono::steady_clock::now();
  struct TraceEndGuard
  {
    const std::string &jobId;
    const std::string &algorithmId;
    std::chrono::steady_clock::time_point start;
    const std::atomic<bool> *dropTrace = nullptr;
    ~TraceEndGuard()
    {
      using namespace sicnu::runtime::observability::trace;
      // R1-5 (#1767): a worker abandoned by a past-deadline shutdown may
      // unwind AFTER process-exit static destruction began — Trace's
      // statics (sink/mutex) are exactly such objects, so late unwinds
      // skip tracing entirely.
      if ( !Trace::enabled() || ( dropTrace && dropTrace->load( std::memory_order_relaxed ) ) )
        return;
      TraceEvent end;
      end.job = jobId;
      end.op = algorithmId;
      end.event = "execution_end";
      end.phase = "end";
      end.durationUs =
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start )
          .count();
      if ( const auto rec = JobEngine::instance().snapshot( jobId ) )
      {
        switch ( rec->state )
        {
        case JobState::Succeeded: end.status = "ok"; break;
        case JobState::Failed: end.status = "error"; break;
        case JobState::Cancelled: end.status = "cancelled"; break;
        default: end.status = "unknown"; break;
        }
        end.detail = rec->error;
      }
      Trace::publish( end );
    }
  } traceEnd{ jobId, request.algorithmId, traceStart, &m_abandonedWorkers };

  // Resolve body. Resolution order (ADR 0062):
  //   per-job executor → prefix executor → RSOperator → fallback (registry).
  std::unique_ptr<sicnu::operators::RSOperator> op;
  if ( !executor )
  {
    op = sicnu::operators::RSOperatorRegistry::instance().create( request.algorithmId );
    if ( !op )
    {
      // Native RSOperator missed: try the registry fallback (e.g. provider
      // algorithms gdal:/otb:/native: that live in AtomicAlgorithmRegistry).
      if ( fallback )
        executor = std::move( fallback );
      else
      {
        JobRecord copy;
        {
          std::lock_guard<std::mutex> lock( m_mutex );
          auto it = m_jobs.find( jobId );
          if ( it == m_jobs.end() )
            return;
          JobRecord &rec = it->second;
          rec.state = JobState::Failed;
          rec.error = "Unknown algorithm: " + request.algorithmId;
          appendLog( rec, JobLogLevel::Error, rec.error );
          finishJobLocked( rec, wasExclusive );
          copy = rec;
        }
        notify( copy );
        return;
      }
    }
  }

  sicnu::operators::RSOperatorContext ctx;
  ctx.setCancelFlag( cancelFlag.get() );

  ctx.setLogCallback( [this, jobId]( const std::string &message, const std::string &level ) {
    // #634/#638: log lines were notified with a FULL JobRecord copy (including
    // result + the entire growing logLines) on every line — a chatty operator
    // paid O(n^2) memory traffic. Ship a delta instead: id + only the lines
    // appended since the previous notify. TaskCenter keys on record.id and
    // uses logLinesOffset to append the slice exactly once; the full
    // cumulative record is still reachable via snapshot() and the terminal
    // notify. (The first cut of this delta omitted id and shipped a 1-line
    // slice, so TaskCenter silently dropped every streaming log line.)
    //
    // #1705: offsets reference the ABSOLUTE appended-line space
    // (droppedLogLines + local index), so the bounded window's front-erase
    // never renumbers what consumers have already indexed — TaskCenter's
    // forwarded-count stays monotonic and exactly-once across evictions.
    JobRecord copy;
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( jobId );
      if ( it == m_jobs.end() )
        return;
      // #1693: a late log line from the operator's own lingering threads
      // after the terminal transition used to appendLog into the FINISHED
      // record and re-insert m_deltaLogCursor[jobId] (finishJobLocked had
      // just erased it) — mutating a terminal record and leaking a cursor
      // entry nothing ever cleans up. Terminal records are immutable.
      if ( isTerminalState( it->second.state ) )
        return;
      appendLog( it->second, logLevelFromString( level ), message );
      copy.id = it->second.id;
      copy.progress = it->second.progress;
      copy.state = it->second.state;
      const std::size_t total = it->second.logLines.size();
      const std::size_t absTotal = total + it->second.droppedLogLines;
      std::size_t &cursor = m_deltaLogCursor[jobId]; // absolute shipped index
      if ( absTotal > cursor )
      {
        // Local start of the un-shipped tail; clamped to the retained
        // window when the cursor predates evicted lines (the dropped lines
        // were already shipped to the consumer — or nobody was listening).
        const std::size_t localStart =
            cursor > it->second.droppedLogLines ? cursor - it->second.droppedLogLines : 0;
        copy.logLinesOffset = it->second.droppedLogLines + localStart + 1;
        copy.logLines.reserve( total - localStart );
        for ( std::size_t i = localStart; i < total; ++i )
          copy.logLines.push_back( it->second.logLines[i] );
        cursor = absTotal;
      }
    }
    notify( copy );
  } );

  ctx.setProgressCallback( [this, jobId]( double progress, const std::string &message ) {
    JobRecord copy;
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( jobId );
      if ( it == m_jobs.end() )
        return;
      // #1803 RIO-9: same terminal-immutability gate as the log callback
      // (#1693) — a late progress tick from the operator's own lingering
      // threads must not mutate (and re-notify) a terminal record.
      if ( isTerminalState( it->second.state ) )
        return;
      // Skip the whole-record copy + notify when nothing observable changed:
      // high-frequency ticks with the same value were the main source of
      // per-callback O(logs) churn (#634). New log lines still get through —
      // the log callback notifies with its own record copy.
      const bool unchanged = it->second.progress == progress
                             && ( message.empty() || it->second.statusMessage == message );
      if ( unchanged )
        return;
      it->second.progress = progress;
      if ( !message.empty() )
        it->second.statusMessage = message;
      copy = it->second;
    }
    notify( copy );
  } );

  // #1640: the four finish* paths below each carried a verbatim copy of the
  // same locked terminal transition (m_jobs lookup + early-out → cancel
  // decision → state/status/error/log → finishJobLocked → copy → notify), so a
  // change to the cancel semantics or to the finish bookkeeping had to be
  // replayed by hand in all four. Describe the outcome as data and apply it in
  // one place instead.
  struct TerminalOutcome
  {
    bool cancelled = false;             ///< Cancelled wins over `state`
    JobState state = JobState::Failed;  ///< terminal state when not cancelled
    std::string statusMessage;
    std::optional<std::string> error;   ///< nullopt keeps the record's error
    JobLogLevel logLevel = JobLogLevel::Error;
    std::string logText;
    bool complete = false;              ///< success path reports progress == 1.0
    bool hasResult = false;             ///< only the success path carries a result
    Json::Value result;
  };

  /// Cancel decision in one place (#1640): an operator-reported Cancelled code,
  /// or a flag armed by cancel()/shutdown (which may race ahead of the worker).
  auto wasCancelled = [&]( bool operatorReportedCancel = false ) {
    return operatorReportedCancel || ( cancelFlag && cancelFlag->load() );
  };

  /// Outcome of a failed / cancelled run (#1640): state, status message, error
  /// and log line all follow from the cancel decision, so they are derived once
  /// here instead of being spelled out per finish* path. `cancelLogText` is
  /// passed explicitly because the unknown-exception path logs its message
  /// verbatim while the others prefix it with "Cancelled: ".
  auto failureOutcome = []( bool cancelled, const std::string &failedMessage,
                            const std::string &cancelMessage,
                            const std::string &cancelLogText ) {
    TerminalOutcome outcome;
    if ( cancelled )
    {
      outcome.cancelled = true;
      outcome.statusMessage = "Cancelled";
      outcome.error = cancelMessage;
      outcome.logLevel = JobLogLevel::Info;
      outcome.logText = cancelLogText;
      return outcome;
    }
    outcome.state = JobState::Failed;
    outcome.statusMessage = "Failed";
    outcome.error = failedMessage;
    outcome.logText = failedMessage;
    return outcome;
  };

  /// The single locked terminal transition shared by every finish* path.
  auto finishTerminal = [&]( TerminalOutcome outcome ) {
    JobRecord copy;
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( jobId );
      if ( it == m_jobs.end() )
        return;
      JobRecord &rec = it->second;
      rec.state = outcome.cancelled ? JobState::Cancelled : outcome.state;
      rec.statusMessage = std::move( outcome.statusMessage );
      if ( outcome.error )
        rec.error = std::move( *outcome.error );
      if ( outcome.complete )
        rec.progress = 1.0;
      if ( outcome.hasResult )
        rec.result = std::move( outcome.result );
      appendLog( rec, outcome.logLevel, outcome.logText );
      copy = rec;
      // #1705: once bounded eviction can shrink logLines below what a
      // consumer has already indexed, a CUMULATIVE terminal record can no
      // longer deliver its final line (TaskCenter only appends when
      // size > forwarded). Ship the terminal line as the SAME monotonic
      // delta stream the log callback uses whenever a cursor exists; the
      // record's fields (state/result/error) ride along unchanged.
      auto cursorIt = m_deltaLogCursor.find( jobId );
      if ( cursorIt != m_deltaLogCursor.end() )
      {
        const std::size_t total = rec.logLines.size();
        const std::size_t absTotal = total + rec.droppedLogLines;
        std::size_t &cursor = cursorIt->second;
        if ( absTotal > cursor )
        {
          const std::size_t localStart =
              cursor > rec.droppedLogLines ? cursor - rec.droppedLogLines : 0;
          copy.logLinesOffset = rec.droppedLogLines + localStart + 1;
          copy.logLines.assign( rec.logLines.begin() + static_cast<std::ptrdiff_t>( localStart ),
                                rec.logLines.end() );
          cursor = absTotal;
        }
        else
        {
          copy.logLines.clear();
          copy.logLinesOffset = 0; // nothing new: fields-only notify
        }
      }
      finishJobLocked( rec, wasExclusive );
    }
    notify( copy );
  };

  auto finishSuccess = [&]( Json::Value result ) {
    // #1803 RIO-10: an executor that RETURNED A RESULT has completed its
    // work — a cancel flag that was armed in the final microseconds (or an
    // operator that finishes despite cancellation) must not rebrand the
    // completed run as Cancelled and silently discard the result. The
    // terminal state is Succeeded, with the late cancel recorded in the
    // log; a job that observed its cancel flag and chose to abort reports
    // the Cancelled OPERATOR CODE instead (handled by finishError below).
    const bool lateCancel = static_cast<bool>( cancelFlag && cancelFlag->load() );
    finishTerminal( { .state = JobState::Succeeded,
                      .statusMessage = lateCancel ? "Succeeded (cancel arrived after completion)"
                                                  : "Succeeded",
                      .logLevel = JobLogLevel::Info,
                      .logText = lateCancel ? "Succeeded; cancel arrived after completion"
                                            : "Succeeded",
                      .complete = true,
                      .hasResult = true,
                      .result = std::move( result ) } );
  };

  auto finishError = [&]( const sicnu::operators::RSOperatorError &e ) {
    const std::string &message = e.message();
    finishTerminal( failureOutcome(
      wasCancelled( e.code() == sicnu::operators::ErrorCode::Cancelled ),
      message, message, "Cancelled: " + message ) );
  };

  auto finishStdException = [&]( const std::exception &e ) {
    const std::string message = e.what();
    finishTerminal(
      failureOutcome( wasCancelled(), message, message, "Cancelled: " + message ) );
  };

  auto finishUnknownException = [&]() {
    const std::string cancelMessage = "Unknown exception occurred during cancellation";
    finishTerminal( failureOutcome( wasCancelled(),
                                    "Unknown non-std exception",
                                    cancelMessage, cancelMessage ) );
  };

  // R29-T02 WP09 (H1): an operator that declares host-thread affinity (QGIS
  // layout rendering — fully transparent exports when painted off the host
  // thread) is marshaled onto the Qt application thread IN DISPATCH. No
  // QCoreApplication (or already on the host thread) → run inline here; the
  // operator's own host check then refuses typed where appropriate.
  const bool marshalToHostThread =
      op && op->requiresHostQtThread()
      && QCoreApplication::instance() != nullptr
      && QThread::currentThread() != QCoreApplication::instance()->thread();

  try
  {
    Json::Value result;
    if ( marshalToHostThread )
      result = runOnHostQtThread(
          [&]() -> Json::Value {
            // Direct run() funnel: apply the schema range gate (#1578) — the
            // typed RSOperatorError propagates through finishError below.
            op->validateParams( request.params );
            return op->run( request.params, ctx );
          },
          cancelFlag );
    else if ( executor )
      result = executor( request, ctx );
    else
    {
      // Direct run() funnel: apply the schema range gate (#1578) — the
      // typed RSOperatorError propagates through finishError below.
      op->validateParams( request.params );
      result = op->run( request.params, ctx );
    }
    finishSuccess( std::move( result ) );
  }
  catch ( const sicnu::operators::RSOperatorError &e )
  {
    finishError( e );
  }
  catch ( const std::exception &e )
  {
    finishStdException( e );
  }
  catch ( ... )
  {
    finishUnknownException();
  }
}

} // namespace sicnu::jobs

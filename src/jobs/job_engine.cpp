// JobEngine implementation
#include "job_engine.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <algorithm>
#include <chrono>
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

/// Test-injectable ceiling standing in for hardware_concurrency (#661):
/// 0 = use the host's real core count.
std::atomic<int> s_concurrencyCeilingOverride{ 0 };

} // namespace

int JobEngine::concurrencyCeiling()
{
  const int overrideCores = s_concurrencyCeilingOverride.load( std::memory_order_relaxed );
  if ( overrideCores > 0 )
    return overrideCores;
  const unsigned int hw = std::thread::hardware_concurrency();
  return static_cast<int>( std::max( 1u, hw ) );
}

void JobEngine::setConcurrencyCeilingForTests( int cores )
{
  s_concurrencyCeilingOverride.store( cores, std::memory_order_relaxed );
}

int JobEngine::defaultWorkerCount()
{
  // Throttler cap minus one core reserved for UI; floor of 1 protects
  // single-core machines (#661).
  const int cap = concurrencyCeiling();
  return std::clamp( cap - 1, 1, cap );
}

JobEngine &JobEngine::instance()
{
  static JobEngine e;
  return e;
}

JobEngine::JobEngine()
{
  m_maxWorkers = defaultWorkerCount();
}

JobEngine::~JobEngine()
{
  shutdown();
}

void JobEngine::shutdown()
{
  std::vector<std::thread> toJoin;
  std::vector<JobRecord> cancelledRecords;
  std::vector<std::function<void()>> cancelHooks;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    if ( m_shuttingDown )
      return;
    m_shuttingDown = true;
    // Latched: production shutdown is final (#684). Only shutdownForTests
    // clears it, so a late submit cannot resurrect worker threads while the
    // process is tearing down.
    m_stopped = true;
    m_stop.store( true );
    m_generation++;
    toJoin.swap( m_workers );

    // Arm cancel flags of running jobs and collect cancel hooks
    for ( auto &kv : m_cancelFlags )
    {
      if ( kv.second )
        kv.second->store( true, std::memory_order_release );
    }
    for ( auto &kv : m_jobBodies )
    {
      if ( kv.second.onCancel )
      {
        cancelHooks.push_back( std::move( kv.second.onCancel ) );
        kv.second.onCancel = nullptr;
      }
    }

    while ( !m_queue.empty() )
    {
      const std::string qId = m_queue.front();
      m_queue.pop_front();
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

  for ( auto &t : toJoin )
  {
    if ( t.joinable() )
      t.join();
  }

  {
    std::lock_guard<std::mutex> lock( m_mutex );
    m_shuttingDown = false;
  }
}

void JobEngine::setMaxWorkers( int n )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_maxWorkers = std::max( 1, n );
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
  return submit( std::move( req ), JobExecutor{}, CancelHook{} );
}

std::string JobEngine::submit( JobRequest req, JobExecutor executor, CancelHook onCancel )
{
  std::string id;
  JobRecord copy;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    // m_stopped (latched production shutdown, #684) rejects like
    // m_shuttingDown — unlike m_stop it is never cleared by a submit.
    if ( m_shuttingDown || m_stopped )
    {
      id = "job-" + std::to_string( m_nextId.fetch_add( 1 ) );
      JobRecord rec;
      rec.id = id;
      rec.request = std::move( req );
      rec.state = JobState::Cancelled;
      rec.createdAtMs = nowUnixMs();
      rec.finishedAtMs = rec.createdAtMs;
      rec.statusMessage = m_shuttingDown ? "Cancelled: JobEngine is shutting down"
                                         : "Cancelled: JobEngine has been shut down";
      m_jobs.emplace( id, rec );
      copy = rec;
    }
    else
    {
      if ( m_stop.load() )
      {
        // After shutdownForTests, allow reuse
        m_stop.store( false );
      }

      id = "job-" + std::to_string( m_nextId.fetch_add( 1 ) );

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
      m_queue.push_back( id );
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
        auto qit = std::find( m_queue.begin(), m_queue.end(), jobId );
        if ( qit != m_queue.end() )
          m_queue.erase( qit );
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

void JobEngine::waitUntilIdleForTests( int timeoutMs )
{
  std::unique_lock<std::mutex> lock( m_mutex );
  const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds( timeoutMs );
  while ( !( m_queue.empty() && m_running == 0 ) )
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
  }
  m_cv.notify_all();

  std::vector<std::thread> toJoin;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    toJoin.swap( m_workers );
  }
  for ( auto &t : toJoin )
  {
    if ( t.joinable() )
      t.join();
  }

  {
    std::lock_guard<std::mutex> lock( m_mutex );
    m_queue.clear();
    m_jobs.clear();
    m_cancelFlags.clear();
    m_jobBodies.clear();
    m_prefixExecutors.clear();
    m_fallbackExecutor = nullptr;
    m_maxWorkers = defaultWorkerCount();
    m_running = 0;
    m_exclusiveRunning = false;
    m_listener = nullptr;
    // Explicit test reset: clears the production-shutdown latch (#684) so
    // the engine remains reusable after a real shutdown() in-process.
    m_stopped = false;
    m_stop.store( false );
  }
  m_cv.notify_all();
}

void JobEngine::ensureWorkersLocked()
{
  if ( m_shuttingDown || m_stopped )
    return;
  const uint64_t gen = m_generation;
  while ( static_cast<int>( m_workers.size() ) < m_maxWorkers )
  {
    m_workers.emplace_back( [this, gen] { workerLoop( gen ); } );
  }
}

void JobEngine::workerLoop( uint64_t gen )
{
  while ( true )
  {
    std::string jobId;
    {
      std::unique_lock<std::mutex> lock( m_mutex );
      for ( ;; )
      {
        if ( m_shuttingDown || m_stopped || gen != m_generation || m_stop.load() )
          return;
        auto picked = tryPickJobLocked();
        if ( picked.has_value() )
        {
          jobId = std::move( *picked );
          break;
        }
        m_cv.wait( lock );
        if ( m_shuttingDown || m_stopped || gen != m_generation || m_stop.load() )
          return;
      }
    }

    runOperatorJob( jobId );
    m_cv.notify_all();
  }
}

std::optional<std::string> JobEngine::tryPickJobLocked()
{
  // Exclusive policy: drain in-flight work, then run exclusive alone.
  // See class comment on JobEngine.
  if ( m_queue.empty() || m_exclusiveRunning )
    return std::nullopt;

  // Best-priority pick (stable: earliest arrival wins ties) so a burst of
  // submissions cannot invert the caller's priority order inside the engine
  // queue (#686). Exclusive and non-exclusive candidates are tracked
  // separately so the drain-then-exclusive policy is untouched (ADR 0002).
  auto bestExclusive = m_queue.end();
  auto bestNonExclusive = m_queue.end();
  int bestExclusivePriority = 0;
  int bestNonExclusivePriority = 0;
  for ( auto it = m_queue.begin(); it != m_queue.end(); ++it )
  {
    auto jit = m_jobs.find( *it );
    if ( jit == m_jobs.end() || jit->second.state != JobState::Queued )
      continue;
    const bool exclusive = jit->second.request.exclusive;
    const int priority = jit->second.request.priority;
    auto &best = exclusive ? bestExclusive : bestNonExclusive;
    int &bestPriority = exclusive ? bestExclusivePriority : bestNonExclusivePriority;
    if ( best == m_queue.end() || priority < bestPriority )
    {
      best = it;
      bestPriority = priority;
    }
  }

  if ( bestExclusive != m_queue.end() )
  {
    if ( m_running > 0 )
      return std::nullopt; // drain before exclusive

    const std::string id = *bestExclusive;
    auto jit = m_jobs.find( id );
    m_queue.erase( bestExclusive );
    if ( jit == m_jobs.end() || jit->second.state != JobState::Queued )
      return std::nullopt;

    jit->second.state = JobState::Running;
    jit->second.startedAtMs = nowUnixMs();
    m_running += 1;
    m_exclusiveRunning = true;
    return id;
  }

  if ( m_running >= m_maxWorkers )
    return std::nullopt;

  if ( bestNonExclusive == m_queue.end() )
    return std::nullopt;

  const std::string id = *bestNonExclusive;
  auto jit = m_jobs.find( id );
  m_queue.erase( bestNonExclusive );
  if ( jit == m_jobs.end() || jit->second.state != JobState::Queued )
    return std::nullopt;

  jit->second.state = JobState::Running;
  jit->second.startedAtMs = nowUnixMs();
  m_running += 1;
  return id;
}

void JobEngine::appendLog( JobRecord &rec, JobLogLevel level, const std::string &text )
{
  JobLogLine line;
  line.unixMs = nowUnixMs();
  line.level = level;
  line.text = text;
  rec.logLines.push_back( std::move( line ) );
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
  m_cancelFlags.erase( rec.id );
  m_jobBodies.erase( rec.id );
  m_deltaLogCursor.erase( rec.id );
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

    auto bit = m_jobBodies.find( jobId );
    if ( bit != m_jobBodies.end() && bit->second.executor )
      executor = bit->second.executor;
    else
      executor = findPrefixExecutorLocked( request.algorithmId );
    // Snapshot the catch-all so registry lookup happens off the engine lock.
    fallback = m_fallbackExecutor;
  }
  notify( startedCopy );

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
    JobRecord copy;
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( jobId );
      if ( it == m_jobs.end() )
        return;
      appendLog( it->second, logLevelFromString( level ), message );
      copy.id = it->second.id;
      copy.progress = it->second.progress;
      copy.state = it->second.state;
      const std::size_t total = it->second.logLines.size();
      std::size_t &cursor = m_deltaLogCursor[jobId];
      if ( total > cursor )
      {
        // 1-based: logLinesOffset == 0 is reserved for cumulative records.
        copy.logLinesOffset = cursor + 1;
        copy.logLines.reserve( total - cursor );
        for ( std::size_t i = cursor; i < total; ++i )
          copy.logLines.push_back( it->second.logLines[i] );
        cursor = total;
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

  auto finishSuccess = [&]( Json::Value result ) {
    JobRecord copy;
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( jobId );
      if ( it == m_jobs.end() )
        return;
      JobRecord &rec = it->second;
      rec.result = std::move( result );
      if ( cancelFlag && cancelFlag->load() )
      {
        rec.state = JobState::Cancelled;
        rec.statusMessage = "Cancelled";
        appendLog( rec, JobLogLevel::Info, "Cancelled" );
      }
      else
      {
        rec.state = JobState::Succeeded;
        rec.progress = 1.0;
        rec.statusMessage = "Succeeded";
        appendLog( rec, JobLogLevel::Info, "Succeeded" );
      }
      finishJobLocked( rec, wasExclusive );
      copy = rec;
    }
    notify( copy );
  };

  auto finishError = [&]( const sicnu::operators::RSOperatorError &e ) {
    JobRecord copy;
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( jobId );
      if ( it == m_jobs.end() )
        return;
      JobRecord &rec = it->second;
      const bool cancelled = ( e.code() == sicnu::operators::ErrorCode::Cancelled )
                             || ( cancelFlag && cancelFlag->load() );
      if ( cancelled )
      {
        rec.state = JobState::Cancelled;
        rec.statusMessage = "Cancelled";
        rec.error = e.message();
        appendLog( rec, JobLogLevel::Info, "Cancelled: " + e.message() );
      }
      else
      {
        rec.state = JobState::Failed;
        rec.statusMessage = "Failed";
        rec.error = e.message();
        appendLog( rec, JobLogLevel::Error, e.message() );
      }
      finishJobLocked( rec, wasExclusive );
      copy = rec;
    }
    notify( copy );
  };

  auto finishStdException = [&]( const std::exception &e ) {
    JobRecord copy;
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( jobId );
      if ( it == m_jobs.end() )
        return;
      JobRecord &rec = it->second;
      const bool cancelled = ( cancelFlag && cancelFlag->load() );
      if ( cancelled )
      {
        rec.state = JobState::Cancelled;
        rec.statusMessage = "Cancelled";
        rec.error = e.what();
        appendLog( rec, JobLogLevel::Info, "Cancelled: " + std::string( e.what() ) );
      }
      else
      {
        rec.state = JobState::Failed;
        rec.statusMessage = "Failed";
        rec.error = e.what();
        appendLog( rec, JobLogLevel::Error, e.what() );
      }
      finishJobLocked( rec, wasExclusive );
      copy = rec;
    }
    notify( copy );
  };

  auto finishUnknownException = [&]() {
    JobRecord copy;
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( jobId );
      if ( it == m_jobs.end() )
        return;
      JobRecord &rec = it->second;
      const bool cancelled = ( cancelFlag && cancelFlag->load() );
      if ( cancelled )
      {
        rec.state = JobState::Cancelled;
        rec.statusMessage = "Cancelled";
        rec.error = "Unknown exception occurred during cancellation";
        appendLog( rec, JobLogLevel::Info, rec.error );
      }
      else
      {
        rec.state = JobState::Failed;
        rec.statusMessage = "Failed";
        rec.error = "Unknown non-std exception";
        appendLog( rec, JobLogLevel::Error, rec.error );
      }
      finishJobLocked( rec, wasExclusive );
      copy = rec;
    }
    notify( copy );
  };

  try
  {
    Json::Value result;
    if ( executor )
      result = executor( request, ctx );
    else
      result = op->run( request.params, ctx );
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

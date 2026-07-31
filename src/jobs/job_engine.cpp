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

} // namespace

JobEngine &JobEngine::instance()
{
  static JobEngine e;
  return e;
}

JobEngine::JobEngine() = default;

JobEngine::~JobEngine()
{
  shutdown();
}

void JobEngine::shutdown()
{
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    if ( m_stop.load() )
      return;
    m_stop.store( true );
  }
  m_cv.notify_all();
  for ( auto &t : m_workers )
  {
    if ( t.joinable() )
      t.join();
  }
  m_workers.clear();
}

void JobEngine::setMaxWorkers( int n )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_maxWorkers = std::clamp( n, 2, 4 );
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
  m_cv.notify_all();
  notify( copy );
  return id;
}

bool JobEngine::cancel( const std::string &jobId )
{
  JobRecord copy;
  bool changed = false;
  CancelHook cancelHook;
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
        m_jobBodies.erase( jobId );
        copy = rec;
        changed = true;
        break;
      }
      case JobState::Running:
      {
        auto fit = m_cancelFlags.find( jobId );
        if ( fit != m_cancelFlags.end() && fit->second )
          fit->second->store( true );
        auto bit = m_jobBodies.find( jobId );
        if ( bit != m_jobBodies.end() && bit->second.onCancel )
          cancelHook = bit->second.onCancel;
        // Terminal state set when operator observes cancel / exits
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
        return true;
      }
    }
  }

  if ( changed )
  {
    m_cv.notify_all();
    notify( copy );
  }
  return changed;
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
    m_running = 0;
    m_exclusiveRunning = false;
    m_listener = nullptr;
    m_stop.store( false );
  }
  m_cv.notify_all();
}

void JobEngine::ensureWorkersLocked()
{
  while ( static_cast<int>( m_workers.size() ) < m_maxWorkers )
  {
    m_workers.emplace_back( [this] { workerLoop(); } );
  }
}

void JobEngine::workerLoop()
{
  while ( true )
  {
    std::string jobId;
    {
      std::unique_lock<std::mutex> lock( m_mutex );
      for ( ;; )
      {
        if ( m_stop.load() )
          return;
        auto picked = tryPickJobLocked();
        if ( picked.has_value() )
        {
          jobId = std::move( *picked );
          break;
        }
        m_cv.wait( lock );
        if ( m_stop.load() )
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

  bool exclusiveQueued = false;
  for ( const auto &id : m_queue )
  {
    auto it = m_jobs.find( id );
    if ( it != m_jobs.end() && it->second.request.exclusive )
    {
      exclusiveQueued = true;
      break;
    }
  }

  if ( exclusiveQueued )
  {
    if ( m_running > 0 )
      return std::nullopt; // drain before exclusive

    for ( auto it = m_queue.begin(); it != m_queue.end(); ++it )
    {
      auto jit = m_jobs.find( *it );
      if ( jit == m_jobs.end() || jit->second.state != JobState::Queued )
        continue;
      if ( !jit->second.request.exclusive )
        continue;

      const std::string id = *it;
      m_queue.erase( it );
      jit->second.state = JobState::Running;
      jit->second.startedAtMs = nowUnixMs();
      m_running += 1;
      m_exclusiveRunning = true;
      return id;
    }
    return std::nullopt;
  }

  if ( m_running >= m_maxWorkers )
    return std::nullopt;

  while ( !m_queue.empty() )
  {
    const std::string id = m_queue.front();
    m_queue.pop_front();
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
    listener( rec );
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
}

void JobEngine::runOperatorJob( const std::string &jobId )
{
  JobRequest request;
  std::shared_ptr<std::atomic<bool>> cancelFlag;
  bool wasExclusive = false;
  JobExecutor executor;

  JobRecord startedCopy;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    auto it = m_jobs.find( jobId );
    if ( it == m_jobs.end() )
      return;

    JobRecord &rec = it->second;
    request = rec.request;
    wasExclusive = request.exclusive;
    cancelFlag = std::make_shared<std::atomic<bool>>( false );
    m_cancelFlags[jobId] = cancelFlag;
    appendLog( rec, JobLogLevel::Info, "Started" );
    startedCopy = rec;

    auto bit = m_jobBodies.find( jobId );
    if ( bit != m_jobBodies.end() && bit->second.executor )
      executor = bit->second.executor;
    else
      executor = findPrefixExecutorLocked( request.algorithmId );
  }
  notify( startedCopy );

  // Resolve body: per-job / prefix executor, else RSOperator.
  std::unique_ptr<sicnu::operators::RSOperator> op;
  if ( !executor )
  {
    op = sicnu::operators::RSOperatorRegistry::instance().create( request.algorithmId );
    if ( !op )
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

  sicnu::operators::RSOperatorContext ctx;
  ctx.setCancelFlag( cancelFlag.get() );

  ctx.setLogCallback( [this, jobId]( const std::string &message, const std::string &level ) {
    JobRecord copy;
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      auto it = m_jobs.find( jobId );
      if ( it == m_jobs.end() )
        return;
      appendLog( it->second, logLevelFromString( level ), message );
      copy = it->second;
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
      if ( cancelFlag->load() )
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
                             || cancelFlag->load();
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
      rec.state = JobState::Failed;
      rec.statusMessage = "Failed";
      rec.error = e.what();
      appendLog( rec, JobLogLevel::Error, e.what() );
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
}

} // namespace sicnu::jobs

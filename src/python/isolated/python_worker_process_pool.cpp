#include "python_worker_process_pool.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

namespace sicnu::python::isolated
{

namespace
{
/// Error message carried by every recovered request the pool could NOT
/// re-dispatch after exhausting its replay budget.
constexpr auto kReplayBudgetExhausted = "Worker crashed; replay budget exhausted";
/// Grace window for a socket EOF with NOTHING in flight: an exiting worker's
/// socket close is usually observed before QProcess's finished(), so an
/// instantaneous isRunning() check cannot distinguish "daemon dropped the
/// connection but lives" from "worker exiting". One deferred re-check inside
/// this window resolves the ambiguity without burning crash budget on clean
/// exits.
constexpr int kIdleDisconnectGraceMs = 250;
/// A (re)spawned worker must complete its connect handshake within this
/// window or its node is treated as lost — otherwise a worker that starts
/// but never attaches (import hang) sits in limbo: alive, unacquirable, yet
/// counted available. Deliberately AFTER the 5 s recovery-request watchdog
/// so a recovery cycle's pending callers are answered before the node is
/// recycled into the next cycle.
constexpr int kRestartHandshakeTimeoutMs = 9000;
} // namespace

bool replayRecoveredRequest( PythonIpcServer *server,
                             const PythonIpcServer::PendingRequest &req,
                             const QString &failureMessage )
{
  // sendRequest() returns -1 WITHOUT registering the callback when the
  // server has no connected client — re-dispatching into that silent drop
  // would strand the recovered caller forever. The failure branch answers
  // the request instead (typed error, isError=true).
  if ( server && req.retriesLeft > 0 )
  {
    const int sentId = server->sendRequest( req.method, req.params, req.callback, req.retriesLeft - 1 );
    if ( sentId >= 0 )
      return true;
  }
  QJsonObject err;
  err[QStringLiteral( "message" )] = failureMessage;
  req.callback( err, true );
  return false;
}

PythonWorkerProcessPool::PythonWorkerProcessPool( int poolSize, QObject *parent )
  : QObject( parent )
  , m_poolSize( poolSize )
{
}

PythonWorkerProcessPool::~PythonWorkerProcessPool()
{
  shutdown();
}

bool PythonWorkerProcessPool::initialize( const QString &pythonPath, const QString &scriptPath )
{
  m_pythonPath = pythonPath;
  m_scriptPath = scriptPath;
  m_shuttingDown = false;

  int created = 0;
  for ( int i = 0; i < m_poolSize; ++i )
  {
    WorkerNode *node = createWorkerNode( m_nextWorkerId++ );
    if ( node )
    {
      QMutexLocker lock( &m_nodesMutex );
      m_nodes.append( node );
      ++created;
    }
  }
  return created > 0;
}

void PythonWorkerProcessPool::shutdown(){
  if ( m_shuttingDown )
    return;
  // A node lost during shutdown is an intended stop, not a channel failure:
  // the guard keeps the recovery handlers from scheduling restarts for the
  // workers we are about to kill ourselves.
  m_shuttingDown = true;
  // Recovered requests waiting on a restart-backoff timer must be answered
  // HERE: their sole owner is m_pendingRecovery, and a timer that never
  // fires (pool dying) must not take the callbacks with it uninvoked. The
  // entries are moved out and the map cleared BEFORE any callback runs — a
  // re-entrant callback cannot invalidate the iteration (and the timer
  // lambda's m_shuttingDown guard keeps it from re-consuming entries).
  std::vector<std::shared_ptr<std::vector<PythonIpcServer::PendingRequest>>> survivors;
  survivors.reserve( m_pendingRecovery.size() );
  for ( auto &entry : m_pendingRecovery )
    survivors.push_back( std::move( entry.second ) );
  m_pendingRecovery.clear();
  for ( const auto &pending : survivors )
    failPendingRequests( *pending, QStringLiteral( "Worker pool is shutting down" ) );
  // Detach the node list first (#1462): the teardown below invokes request
  // callbacks, which must never run under m_nodesMutex (they may re-enter
  // the pool), and a concurrent acquireWorker must see an empty list rather
  // than nodes mid-deletion.
  QList<WorkerNode *> nodes;
  {
    QMutexLocker lock( &m_nodesMutex );
    nodes = m_nodes;
    m_nodes.clear();
  }
  for ( WorkerNode *node : nodes )
  {
    if ( node )
    {
      // In-flight requests must be answered, not leaked with the deleted
      // server — their callers otherwise hang on a pool that is already gone.
      if ( node->server )
      {
        failPendingRequests( node->server->takeInFlightRequests(),
                             QStringLiteral( "Worker pool is shutting down" ) );
        node->server->disconnect();
        node->server->close();
        delete node->server;
        node->server = nullptr;
      }
      if ( node->worker )
      {
        node->worker->disconnect();
        node->worker->stopWorker();
        delete node->worker;
        node->worker = nullptr;
      }
      delete node;
    }
  }
}

WorkerNode *PythonWorkerProcessPool::acquireWorker( int timeoutMs )
{
  // processEvents() is only legal on the thread that owns the application
  // object; background callers must not spin the GUI event loop (#527).
  // A default-argument caller may run before any QCoreApplication exists
  // (the pool is a plain QObject), so treat that as "not the main thread".
  const bool onMainThread =
      QCoreApplication::instance()
      && QThread::currentThread() == QCoreApplication::instance()->thread();
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs > 0 ? timeoutMs : 0 );
  for ( ;; )
  {
    {
      QMutexLocker lock( &m_nodesMutex );
      for ( WorkerNode *node : m_nodes )
      {
        if ( !node || node->isRestarting.load( std::memory_order_acquire ) ||
             node->isBusy.load( std::memory_order_relaxed ) )
        {
          continue;
        }
        if ( node->worker && node->worker->isRunning() &&
             node->server && node->server->hasClient() )
        {
          node->isBusy.store( true, std::memory_order_release );
          return node;
        }
      }
    }
    if ( std::chrono::steady_clock::now() >= deadline )
      return nullptr;
    if ( onMainThread )
      QCoreApplication::processEvents();
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
  }
}

void PythonWorkerProcessPool::releaseWorker( WorkerNode *node )
{
  if ( node )
  {
    QMutexLocker lock( &m_nodesMutex );
    node->isBusy.store( false, std::memory_order_release );
    // Dead-worker recycling: a worker that survived a full job cycle earns
    // back one crash-restart step (bounded by the budget it started with), so
    // a long-lived pool's nodes are not retired over transient crashes that
    // happened long ago. The cumulative restartCount stays untouched — it is
    // the health metric, not the budget.
    if ( node->worker && node->worker->isRunning() )
    {
      int current = node->crashBudgetLeft.load( std::memory_order_relaxed );
      while ( current < kMaxWorkerCrashRestarts &&
              !node->crashBudgetLeft.compare_exchange_weak( current, current + 1, std::memory_order_release, std::memory_order_relaxed ) )
      {
      }
    }
  }
}

int PythonWorkerProcessPool::activeWorkerCount() const
{
  QMutexLocker lock( &m_nodesMutex );
  int count = 0;
  for ( const WorkerNode *node : m_nodes )
  {
    if ( node && node->worker && node->worker->isRunning() )
    {
      count++;
    }
  }
  return count;
}

bool PythonWorkerProcessPool::setPoolSize( int newSize )
{
  if ( newSize < 1 )
    return false;

  if ( newSize > m_poolSize )
  {
    // ── Grow: add new worker nodes ── (createWorkerNode spawns processes —
    // keep the slow part outside m_nodesMutex.)
    QList<WorkerNode *> created;
    for ( int i = m_poolSize; i < newSize; ++i )
    {
      WorkerNode *node = createWorkerNode( m_nextWorkerId++ );
      if ( node )
        created.append( node );
    }
    QMutexLocker lock( &m_nodesMutex );
    m_nodes.append( created );
  }
  else if ( newSize < m_poolSize )
  {
    // ── Shrink: remove idle (non-busy) nodes from the tail ──
    QList<WorkerNode *> removed;
    {
      QMutexLocker lock( &m_nodesMutex );
      const int excessNodes = (std::max)( 0, static_cast<int>( m_nodes.size() ) - newSize );

      // Upfront transactional check: ensure enough idle nodes exist before mutating
      int availableIdle = 0;
      for ( const WorkerNode *node : m_nodes )
      {
        if ( node && !node->isBusy && !node->isRestarting )
          availableIdle++;
      }
      if ( availableIdle < excessNodes )
        return false;

      int toRemove = excessNodes;
      for ( int i = m_nodes.size() - 1; i >= 0 && toRemove > 0; --i )
      {
        WorkerNode *node = m_nodes[i];
        // Skip busy and mid-restart nodes — a pending restart timer still
        // holds the id; removing the node makes the timer bail (#1095a).
        if ( node && !node->isBusy && !node->isRestarting )
        {
          removed.append( node );
          m_nodes.removeAt( i );
          --toRemove;
        }
      }
    }
    // Teardown runs OUTSIDE the lock: it answers in-flight request
    // callbacks, which must never run under m_nodesMutex (#1462).
    for ( WorkerNode *node : removed )
    {
      // An idle node can still hold UNANSWERED requests (the owner released
      // it while a response was in flight). Deleting the server would leak
      // those callbacks mid-air; answer them typed instead. The same for a
      // recovery entry already waiting on this node's backoff timer —
      // without this the entry would be orphaned by the deletion (its
      // watchdog dies with the server).
      if ( node->server )
      {
        failPendingRequests( node->server->takeInFlightRequests(),
                             QStringLiteral( "Worker removed during pool resize" ) );
        node->server->disconnect();
        node->server->close();
        delete node->server;
      }
      const auto pendingEntry = m_pendingRecovery.find( node->id );
      if ( pendingEntry != m_pendingRecovery.end() )
      {
        failPendingRequests( *pendingEntry->second, QStringLiteral( "Worker removed during pool resize" ) );
        m_pendingRecovery.erase( pendingEntry );
      }
      if ( node->worker )
      {
        node->worker->disconnect();
        node->worker->stopWorker();
        delete node->worker;
      }
      delete node;
    }
  }

  m_poolSize = newSize;
  return true;
}

int PythonWorkerProcessPool::availableWorkerCount() const
{
  QMutexLocker lock( &m_nodesMutex );
  int count = 0;
  for ( const WorkerNode *node : m_nodes )
  {
    // Available == acquirable: a worker that runs but has no connected
    // client refuses every send, so it must not count as capacity. The
    // handshake watchdog recycles such nodes; until it fires, this count
    // must not lie to capacity planning.
    if ( node && !node->isBusy && node->worker && node->worker->isRunning()
         && node->server && node->server->hasClient() )
    {
      count++;
    }
  }
  return count;
}

PoolHealthSnapshot PythonWorkerProcessPool::poolHealth() const
{
  QMutexLocker lock( &m_nodesMutex );
  PoolHealthSnapshot snapshot;
  snapshot.total = m_nodes.size();
  for ( const WorkerNode *node : m_nodes )
  {
    if ( node )
    {
      snapshot.totalRestarts += node->restartCount;
      if ( node->worker && node->worker->isRunning() )
      {
        snapshot.active++;
        // Same acquirable semantics as availableWorkerCount: a running
        // worker without a connected client is not usable capacity.
        if ( !node->isBusy && node->server && node->server->hasClient() )
          snapshot.available++;
      }
    }
  }
  return snapshot;
}

WorkerNode *PythonWorkerProcessPool::createWorkerNode( int id )
{
  auto *node = new WorkerNode();
  node->id = id;
  node->server = new PythonIpcServer();
  node->worker = new PythonWorkerProcess();

  QString socketName = QString( "sicnu_pool_%1_%2" ).arg( QCoreApplication::applicationPid() ).arg( id );
  if ( !node->server->listen( socketName ) )
  {
    delete node->server;
    delete node->worker;
    delete node;
    return nullptr;
  }

  bindNodeSignals( node );

  if ( !node->worker->startWorker( socketName, m_pythonPath, m_scriptPath ) )
  {
    node->worker->disconnect();
    node->server->disconnect();
    node->server->close();
    delete node->server;
    delete node->worker;
    delete node;
    return nullptr;
  }

  armHandshakeWatchdog( node );
  return node;
}

WorkerNode *PythonWorkerProcessPool::findNodeById( int id ) const
{
  QMutexLocker lock( &m_nodesMutex );
  for ( WorkerNode *candidate : m_nodes )
  {
    if ( candidate && candidate->id == id )
      return candidate;
  }
  return nullptr;
}

void PythonWorkerProcessPool::bindNodeSignals( WorkerNode *node, PythonWorkerProcess *worker,
                                                PythonIpcServer *server )
{
  if ( !node )
    return;
  // #1486: on the restart path the caller hands over the pair it installed
  // under m_nodesMutex. Reading node->worker / node->server here would race
  // the handleWorkerLoss() restart that replaces them (and, in the old
  // shape, bind the signals to the NEXT cycle's objects).
  PythonWorkerProcess *workerPtr = worker ? worker : node->worker;
  PythonIpcServer *serverPtr = server ? server : node->server;
  if ( !workerPtr || !serverPtr )
    return;
  const int id = node->id;
  // setPoolSize may delete a WorkerNode while one of these handlers is still
  // queued — every handler re-resolves the node from m_nodes by id (#1095a).
  connect( workerPtr, &PythonWorkerProcess::workerCrashed, this, [this, id]() {
    if ( WorkerNode *live = findNodeById( id ) )
      handleWorkerCrash( live );
  } );
  // A worker that exits CLEANLY (the historical benign whitelist: exit 0/1
  // emits no workerCrashed) while requests are still unanswered is a channel
  // failure all the same — protocol EOF, not a crash. Without this handler
  // the pool never recovers those in-flight callers.
  connect( workerPtr, &PythonWorkerProcess::workerFinished, this, [this, id]( int exitCode, QProcess::ExitStatus ) {
    WorkerNode *live = findNodeById( id );
    if ( !live || m_shuttingDown || live->isRestarting )
      return;
    if ( live->server && live->server->inFlightCount() > 0 )
      handleWorkerLoss( live, QStringLiteral( "worker exited (exitCode=%1) with %2 in-flight request(s) — protocol EOF" )
                                   .arg( exitCode )
                                   .arg( live->server->inFlightCount() ) );
  } );
  // Socket EOF: two worlds produce this signal and they need different
  // responses. (a) Requests in flight on a dead channel — an unambiguous
  // loss, recover now. (b) EOF with nothing in flight is ambiguous: either
  // the daemon is alive and dropped the channel (a loss all the same — every
  // future send on it would silently fail), or the worker is EXITING and its
  // socket close simply beat QProcess's finished() notification: isRunning()
  // is stale-true at this instant, and a loss cycle run on it burns crash
  // budget on a clean exit (five clean exits retire a healthy node) plus a
  // spurious workerCrashed. Defer one grace turn — an exiting worker's
  // finished() lands well inside it and its lane owns the node (retirement
  // when idle, loss when requests were in flight).
  connect( serverPtr, &PythonIpcServer::clientDisconnected, this, [this, id]() {
    WorkerNode *live = findNodeById( id );
    if ( !live || m_shuttingDown || live->isRestarting )
      return;
    if ( !live->worker || !live->server )
      return;
    if ( live->server->inFlightCount() > 0 )
    {
      handleWorkerLoss( live, QStringLiteral( "worker disconnected with %1 in-flight request(s) — protocol EOF" )
                                   .arg( live->server->inFlightCount() ) );
      return;
    }
    QTimer::singleShot( kIdleDisconnectGraceMs, this, [this, id]() {
      if ( m_shuttingDown )
        return;
      WorkerNode *live = findNodeById( id );
      if ( !live || live->isRestarting || !live->worker || !live->server )
        return;
      if ( live->server->hasClient() )
        return; // a fresh connection replaced the lost one inside the grace
      if ( live->worker->isRunning() )
        handleWorkerLoss( live, QStringLiteral( "worker disconnected — protocol EOF" ) );
      // else: the worker was exiting — workerFinished's lane owns the node.
    } );
  } );
}

void PythonWorkerProcessPool::armHandshakeWatchdog( WorkerNode *node, PythonIpcServer *server )
{
  if ( !node )
    return;
  // #1486: on the restart path the caller passes the server it installed
  // under m_nodesMutex. Reading node->server here instead would race the
  // handleWorkerLoss() restart that replaces it — and would parent the
  // watchdog to the NEXT cycle's server, which is precisely what the
  // identity re-check below exists to defend against.
  PythonIpcServer *serverPtr = server ? server : node->server;
  if ( !serverPtr )
    return;
  // Parented to THIS cycle's server so any earlier teardown (next loss
  // cycle, resize, shutdown) kills the timer with it; the server-identity
  // re-check at fire time closes the same-dispatcher-pass deleteLater
  // window — the same discipline as the recovery-request watchdog.
  QPointer<PythonIpcServer> myServer( serverPtr );
  const int id = node->id;
  auto *watchdog = new QTimer( serverPtr );
  watchdog->setSingleShot( true );
  connect( watchdog, &QTimer::timeout, this, [this, id, myServer]() {
    WorkerNode *live = findNodeById( id );
    if ( !myServer || !live || live->server != myServer || live->isRestarting )
      return;
    if ( live->server->hasClient() )
      return; // handshake completed (possibly right at the deadline)
    handleWorkerLoss( live, QStringLiteral( "worker handshake timeout — started but never connected" ) );
  } );
  watchdog->start( kRestartHandshakeTimeoutMs );
}

void PythonWorkerProcessPool::handleWorkerCrash( WorkerNode *node )
{
  if ( !node || node->isRestarting || m_shuttingDown )
    return;

  // The console-side reason carries the REAL classification axis (exit code
  // + crash status) and the captured stderr tail — never a constant string.
  // Read from the crashed worker before it is disposed.
  QString reason = QStringLiteral( "worker crashed" );
  if ( node->worker )
  {
    reason = QStringLiteral( "worker crashed: exitCode=%1 crashExit=%2" )
                 .arg( node->worker->lastExitCode() )
                 .arg( node->worker->lastExitWasCrash() ? 1 : 0 );
    const QByteArray stderrTail = node->worker->capturedStderr().trimmed();
    if ( !stderrTail.isEmpty() )
      reason += QStringLiteral( "; stderr: " )
                + QString::fromUtf8( stderrTail.left( 512 ) );
  }
  handleWorkerLoss( node, reason );
}

void PythonWorkerProcessPool::handleWorkerLoss( WorkerNode *node, const QString &reason )
{
  if ( !node || node->isRestarting.load( std::memory_order_acquire ) || m_shuttingDown )
    return;

  node->isRestarting.store( true, std::memory_order_release );
  int id = node->id;
  qWarning() << "Worker lost in pool, id:" << id << "reason:" << reason;
  emit workerCrashed( id, reason );

  // State recovery: take ownership of any requests that were in flight when
  // the worker was lost so they can be re-dispatched to the restarted worker
  // (ADR 0064). Callbacks are moved out and will not fire on the old server.
  // FIRST give the socket one final parse pass: an answer written by the
  // worker immediately before its death is still sitting in the receive
  // buffer, and the death/data notifiers have no ordering guarantee —
  // draining here turns "worker died on request X" into "request X actually
  // completed" whenever the bytes made it out.
  //
  // Detach the pair from the node UNDER the lock, then tear it down OUTSIDE
  // it — the discipline shutdown() and the setPoolSize shrink path follow
  // (#1462; m_nodesMutex must never be held across pending-request
  // callbacks, which may re-enter the pool and would self-deadlock on the
  // non-recursive QMutex). drainBufferedResponses() parses buffered bytes
  // and INVOKES those callbacks, and stopWorker() blocks up to ~1 s in
  // terminate/waitForFinished/kill — neither may hold the lock. The lock's
  // only job here is the pointer publication (#1486).
  PythonIpcServer *server = nullptr;
  PythonWorkerProcess *worker = nullptr;
  {
    QMutexLocker lock( &m_nodesMutex );
    server = node->server;
    node->server = nullptr;
    worker = node->worker;
    node->worker = nullptr;
  }
  std::vector<PythonIpcServer::PendingRequest> pending;
  if ( server )
  {
    server->drainBufferedResponses();
    pending = server->takeInFlightRequests();
  }
  if ( worker )
  {
    worker->disconnect();
    worker->stopWorker();
    worker->deleteLater();
  }
  if ( server )
  {
    server->disconnect();
    server->close();
    server->deleteLater();
  }

  if ( node->crashBudgetLeft <= 0 )
  {
    qWarning() << "Worker process id:" << id << "exhausted its crash-restart budget (" << kMaxWorkerCrashRestarts
               << "); retiring the node";
    node->isRestarting.store( false, std::memory_order_release );
    failPendingRequests( pending, QStringLiteral( "Worker crash restart budget exhausted" ) );
    // A PREVIOUS cycle's recovery may still be waiting on this node's backoff
    // timer (replacement workers dying pre-connect leave the fresh in-flight
    // list empty). Retiring the node must answer those too, or their callers
    // hang forever — and the watchdog that would normally answer them dies
    // with each cycle's server.
    const auto pendingEntry = m_pendingRecovery.find( id );
    if ( pendingEntry != m_pendingRecovery.end() )
    {
      failPendingRequests( *pendingEntry->second, QStringLiteral( "Worker crash restart budget exhausted" ) );
      m_pendingRecovery.erase( pendingEntry );
    }
    return;
  }

  // Self-healing auto-restart with unique socket name
  // DATAPY-3: keep node reserved for its owner — do not unconditionally clear
  // isBusy; the owning PythonPluginAdapter still holds m_workerNode and re-binds
  // its bridge on workerRestarted. Clearing it would let a second plugin bind
  // a second bridge to the same server (crossed IPC).
  const bool wasBusy = node->isBusy;
  node->crashBudgetLeft--;
  node->restartCount++;
  // Backoff follows the REMAINING budget: a node that earned budget back
  // through releaseWorker restarts responsively again, while a serially
  // crashing worker still climbs to the 10 s cap (DATAPY-4).
  const int backoffMs = std::min( 500 * ( 1 << ( kMaxWorkerCrashRestarts - node->crashBudgetLeft ) ), 10000 );
  // Sole ownership of the recovered requests moves into m_pendingRecovery —
  // NOT into the timer lambda. A pool destroyed during the backoff window
  // answers its survivors from shutdown(); the timer, the replay and the
  // watchdog below only BORROW the entry (and erase it when consumed).
  // Capture id only — setPoolSize may delete the WorkerNode during backoff
  // (#1095a). Re-resolve from m_nodes before touching any fields.
  if ( !pending.empty() )
    m_pendingRecovery[id] = std::make_shared<std::vector<PythonIpcServer::PendingRequest>>( std::move( pending ) );
  QTimer::singleShot( backoffMs, this, [this, wasBusy, id]() {
  // Shutdown answered and cleared everything; a fire after that is a no-op
  // (never spawn a worker, never touch the cleared map mid-answer).
  if ( m_shuttingDown )
    return;
  const auto borrowed = m_pendingRecovery.find( id );
  WorkerNode *node = findNodeById( id );
  if ( !node )
  {
    // Node removed by pool shrink while the restart was pending.
    if ( borrowed != m_pendingRecovery.end() )
    {
      failPendingRequests( *borrowed->second, QStringLiteral( "Worker removed during pool resize" ) );
      m_pendingRecovery.erase( borrowed );
    }
    return;
  }
  QString socketName = QString( "sicnu_pool_%1_%2_%3" )
                         .arg( QCoreApplication::applicationPid() )
                         .arg( id )
                         .arg( node->restartCount.load() );
  auto *newServer = new PythonIpcServer();
  auto *newWorker = new PythonWorkerProcess();

  if ( newServer->listen( socketName ) )
  {
    // #1486: take the installed pair under the SAME lock that published it.
    // Everything below this point runs after isRestarting is cleared, i.e.
    // with handleWorkerLoss()/resize free to replace node->server — so the
    // rest of this cycle must use the snapshot, never re-read the members.
    PythonIpcServer *restartedServer = nullptr;
    {
      QMutexLocker lock( &m_nodesMutex );
      node->server = newServer;
      node->worker = newWorker;
      node->isBusy.store( wasBusy, std::memory_order_release );
      restartedServer = node->server;
    }
    bindNodeSignals( node, newWorker, restartedServer );
    const bool started = newWorker->startWorker( socketName, m_pythonPath, m_scriptPath );
    node->isRestarting.store( false, std::memory_order_release );
    if ( !started )
    {
      // The fresh worker never launched: dispose both allocations exactly as
      // the listen-failure branch below does (#1384) — bindNodeSignals ran,
      // so leaving the dead pair would keep the node unacquirable forever
      // with no signal able to move it into another loss cycle. The node
      // itself stays: a busy node is still owned by its adapter (DATAPY-3),
      // and the budget step is refunded because nothing was restarted.
      {
        QMutexLocker lock( &m_nodesMutex );
        if ( node->server )
        {
          node->server->disconnect();
          node->server->close();
          node->server->deleteLater();
          node->server = nullptr;
        }
        if ( node->worker )
        {
          node->worker->disconnect();
          node->worker->deleteLater();
          node->worker = nullptr;
        }
      }
      if ( !wasBusy )
        node->isBusy.store( false, std::memory_order_release ); // unowned: let shrink/acquire reclaim the slot
      if ( node->crashBudgetLeft < kMaxWorkerCrashRestarts )
        node->crashBudgetLeft++; // refund — the restart never launched
      qWarning() << "Worker restart FAILED for id:" << id
                 << "; node drained (server/worker disposed)";
      // Answer any recovered requests with an error so their callers do not
      // hang on a worker that will never exist.
      if ( borrowed != m_pendingRecovery.end() )
      {
        failPendingRequests( *borrowed->second, QStringLiteral( "Worker restart failed (process did not start)" ) );
        m_pendingRecovery.erase( borrowed );
      }
      return;
    }
    qInfo() << "Successfully auto-healed and restarted worker process id:" << id;
    emit workerRestarted( id );
    armHandshakeWatchdog( node, restartedServer );

    if ( borrowed != m_pendingRecovery.end() && !borrowed->second->empty() )
    {
      // Shared view onto the entry that still lives in m_pendingRecovery, so
      // a shutdown() during the replay window still answers the survivors.
      auto sharedPending = borrowed->second;

      // State recovery: replay lost requests once the fresh worker connects.
      // Each replay consumes one retry; a request whose budget is exhausted —
      // or whose send is refused by a socket that just went away — is
      // answered with an error so the caller never hangs. The erase after
      // the callbacks re-checks entry IDENTITY: a re-entrant callback that
      // consumed (and maybe replaced) the map entry must not make the stale
      // erase drop the replacement.
      const auto replay = [this, id, sharedPending]() {
        const auto entry = m_pendingRecovery.find( id );
        if ( entry == m_pendingRecovery.end() || entry->second != sharedPending )
          return; // consumed elsewhere (shutdown/watchdog answered them)
        WorkerNode *live = findNodeById( id );
        if ( !live || !live->server )
        {
          failPendingRequests( *sharedPending, QStringLiteral( "Worker restart failed (server gone)" ) );
          m_pendingRecovery.erase( id );
          return;
        }
        for ( const PythonIpcServer::PendingRequest &req : *sharedPending )
          replayRecoveredRequest( live->server, req, QString::fromUtf8( kReplayBudgetExhausted ) );
        const auto consumed = m_pendingRecovery.find( id );
        if ( consumed != m_pendingRecovery.end() && consumed->second == sharedPending )
          m_pendingRecovery.erase( consumed );
      };
      if ( !restartedServer )
        return;
      if ( restartedServer->hasClient() )
        replay();
      else
        connect( restartedServer, &PythonIpcServer::clientConnected, this, [replay]() { replay(); } );

      // Watchdog: if the restarted worker never connects (dies during
      // startup), fail the pending callbacks instead of leaking them. The
      // entry is erased on consumption, so a late replay becomes a no-op.
      // The timer is parented to THIS recovery's server — the next
      // handleWorkerLoss for the node deletes that server (watchdog with
      // it) — so a stale watchdog from recovery cycle N can never fire on
      // cycle N+1's freshly inserted entry and answer it as "timed out";
      // the server-identity check closes the residual same-dispatcher-pass
      // window (deleteLater of the old server and an already-expired timer
      // can fire within one event pass).
      QPointer<PythonIpcServer> myServer( restartedServer );
      auto *watchdog = new QTimer( restartedServer );
      watchdog->setSingleShot( true );
      connect( watchdog, &QTimer::timeout, this, [this, id, myServer]() {
        // Only answer while THIS recovery's server is still the node's live
        // one (a newer cycle already replaced it — its entry is not ours).
        WorkerNode *live = findNodeById( id );
        if ( !myServer || !live || live->server != myServer )
          return;
        const auto it = m_pendingRecovery.find( id );
        if ( it == m_pendingRecovery.end() )
          return;
        QJsonObject err;
        err[QStringLiteral( "message" )] = QStringLiteral( "Worker restart timed out" );
        for ( const PythonIpcServer::PendingRequest &req : *it->second )
          req.callback( err, true );
        m_pendingRecovery.erase( it );
      } );
      watchdog->start( 5000 );
    }
    else if ( borrowed != m_pendingRecovery.end() )
    {
      m_pendingRecovery.erase( borrowed ); // nothing to recover
    }
  }
  else
  {
    // Listen failure: the server allocated above never listened and the
    // worker was never started, so bindNodeSignals never ran — no
    // workerCrashed/workerFinished can ever fire for this node again, and
    // without the disposal below the node would sit with dead objects it
    // can never lose (#1384). Dispose both allocations (the node itself
    // stays: a busy node is still owned by its adapter, which re-acquires
    // through the normal !server error path — DATAPY-3) and refund the
    // budget step: nothing was restarted.
    newServer->disconnect();
    newServer->close();
    newServer->deleteLater();
    newWorker->disconnect();
    newWorker->deleteLater();
    {
      QMutexLocker lock( &m_nodesMutex );
      node->server = nullptr;
      node->worker = nullptr;
    }
    node->isRestarting.store( false, std::memory_order_release );
    if ( !wasBusy )
      node->isBusy.store( false, std::memory_order_release ); // unowned: let shrink/acquire reclaim the slot
    if ( node->crashBudgetLeft < kMaxWorkerCrashRestarts )
      node->crashBudgetLeft++; // refund — the restart never launched
    qWarning() << "Worker restart listen failed for id:" << id
               << "; node drained (server/worker disposed)";
    if ( borrowed != m_pendingRecovery.end() )
    {
      failPendingRequests( *borrowed->second, QStringLiteral( "Worker restart failed (listen)" ) );
      m_pendingRecovery.erase( borrowed );
    }
  }
  } );
}

void PythonWorkerProcessPool::failPendingRequests(
  const std::vector<PythonIpcServer::PendingRequest> &pending,
  const QString &message )
{
  for ( const PythonIpcServer::PendingRequest &req : pending )
  {
    QJsonObject err;
    err[QStringLiteral( "message" )] = message;
    req.callback( err, true );
  }
}

PythonIpcServer *PythonWorkerProcessPool::serverForWorker( int id ) const
{
  QMutexLocker lock( &m_nodesMutex );
  for ( const auto &node : m_nodes )
  {
    if ( node && node->id == id )
      return node->server;
  }
  return nullptr;
}

} // namespace sicnu::python::isolated

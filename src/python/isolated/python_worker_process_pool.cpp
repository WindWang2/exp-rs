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

  for ( int i = 0; i < m_poolSize; ++i )
  {
    WorkerNode *node = createWorkerNode( m_nextWorkerId++ );
    if ( node )
    {
      m_nodes.append( node );
    }
  }
  return !m_nodes.isEmpty();
}

void PythonWorkerProcessPool::shutdown()
{
  for ( WorkerNode *node : m_nodes )
  {
    if ( node )
    {
      if ( node->worker )
      {
        node->worker->disconnect();
        node->worker->stopWorker();
        delete node->worker;
        node->worker = nullptr;
      }
      if ( node->server )
      {
        node->server->disconnect();
        node->server->close();
        delete node->server;
        node->server = nullptr;
      }
      delete node;
    }
  }
  m_nodes.clear();
}

WorkerNode *PythonWorkerProcessPool::acquireWorker()
{
  // processEvents() is only legal on the thread that owns the application
  // object; background callers must not spin the GUI event loop (#527).
  const bool onMainThread = QThread::currentThread() == QCoreApplication::instance()->thread();
  for ( int retry = 0; retry < 50; ++retry )
  {
    for ( WorkerNode *node : m_nodes )
    {
      if ( node && !node->isBusy && node->worker && node->worker->isRunning() && node->server && node->server->hasClient() )
      {
        node->isBusy = true;
        return node;
      }
    }
    if ( onMainThread )
      QCoreApplication::processEvents();
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
  }
  return nullptr;
}

void PythonWorkerProcessPool::releaseWorker( WorkerNode *node )
{
  if ( node )
  {
    node->isBusy = false;
  }
}

int PythonWorkerProcessPool::activeWorkerCount() const
{
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
    // ── Grow: add new worker nodes ──
    for ( int i = m_poolSize; i < newSize; ++i )
    {
      WorkerNode *node = createWorkerNode( m_nextWorkerId++ );
      if ( node )
        m_nodes.append( node );
    }
  }
  else if ( newSize < m_poolSize )
  {
    // ── Shrink: remove idle (non-busy) nodes from the tail ──
    const int excessNodes = (std::max)( 0, static_cast<int>( m_nodes.size() ) - newSize );
    
    // Upfront transactional check: ensure enough idle nodes exist before mutating
    int availableIdle = 0;
    for ( const WorkerNode *node : m_nodes )
    {
      if ( node && !node->isBusy )
        availableIdle++;
    }
    if ( availableIdle < excessNodes )
      return false;

    int toRemove = excessNodes;
    for ( int i = m_nodes.size() - 1; i >= 0 && toRemove > 0; --i )
    {
      WorkerNode *node = m_nodes[i];
      if ( node && !node->isBusy )
      {
        if ( node->worker )
        {
          node->worker->disconnect();
          node->worker->stopWorker();
          delete node->worker;
        }
        if ( node->server )
        {
          node->server->disconnect();
          node->server->close();
          delete node->server;
        }
        delete node;
        m_nodes.removeAt( i );
        --toRemove;
      }
    }
  }

  m_poolSize = newSize;
  return true;
}

int PythonWorkerProcessPool::availableWorkerCount() const
{
  int count = 0;
  for ( const WorkerNode *node : m_nodes )
  {
    if ( node && !node->isBusy && node->worker && node->worker->isRunning() )
    {
      count++;
    }
  }
  return count;
}

PoolHealthSnapshot PythonWorkerProcessPool::poolHealth() const
{
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
        if ( !node->isBusy )
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

  connect( node->worker, &PythonWorkerProcess::workerCrashed, this, [this, node]() {
    handleWorkerCrash( node );
  } );

  node->worker->startWorker( socketName, m_pythonPath, m_scriptPath );
  return node;
}

void PythonWorkerProcessPool::handleWorkerCrash( WorkerNode *node )
{
  if ( !node || node->isRestarting )
    return;

  node->isRestarting = true;
  int id = node->id;
  qWarning() << "Worker process crashed in pool, id:" << id;
  emit workerCrashed( id, QStringLiteral( "Process exited unexpectedly / segfault" ) );

  // State recovery: take ownership of any requests that were in flight when
  // the worker died so they can be re-dispatched to the restarted worker
  // (ADR 0064). Callbacks are moved out and will not fire on the old server.
  std::vector<PythonIpcServer::PendingRequest> pending;
  if ( node->server )
    pending = node->server->takeInFlightRequests();

  if ( node->worker )
  {
    node->worker->stopWorker();
    node->worker->deleteLater();
    node->worker = nullptr;
  }
  if ( node->server )
  {
    node->server->close();
    node->server->deleteLater();
    node->server = nullptr;
  }

  constexpr int kMaxWorkerRestarts = 5;
  if ( node->restartCount >= kMaxWorkerRestarts )
  {
    qWarning() << "Worker process id:" << id << "exceeded max crash restarts (" << kMaxWorkerRestarts << "); giving up";
    node->isRestarting = false;
    failPendingRequests( pending );
    return;
  }

  // Self-healing auto-restart with unique socket name
  // DATAPY-3: keep node reserved for its owner — do not unconditionally clear
  // isBusy; the owning PythonPluginAdapter still holds m_workerNode and re-binds
  // its bridge on workerRestarted. Clearing it would let a second plugin bind
  // a second bridge to the same server (crossed IPC).
  const bool wasBusy = node->isBusy;
  node->restartCount++;
  // DATAPY-4: exponential backoff before restart to avoid tight crash loop.
  // Scheduled with a timer instead of msleep (#624): workerCrashed is a
  // direct connection on the GUI thread, and the sleep froze the UI for up
  // to 10 s per crash. The whole restart body runs inside the timer so the
  // crash handler returns immediately.
  const int backoffMs = std::min( 500 * ( 1 << std::min( node->restartCount, 5 ) ), 10000 );
  QTimer::singleShot( backoffMs, this, [this, node, wasBusy, id, pending = std::move( pending )]() mutable {
  QString socketName = QString( "sicnu_pool_%1_%2_%3" )
                         .arg( QCoreApplication::applicationPid() )
                         .arg( id )
                         .arg( node->restartCount );
  node->server = new PythonIpcServer();
  node->worker = new PythonWorkerProcess();
  node->isBusy = wasBusy;

  if ( node->server->listen( socketName ) )
  {
    connect( node->worker, &PythonWorkerProcess::workerCrashed, this, [this, node]() {
      handleWorkerCrash( node );
    } );
    const bool started = node->worker->startWorker( socketName, m_pythonPath, m_scriptPath );
    node->isRestarting = false;
    if ( !started )
    {
      // The fresh worker never launched: answer any recovered requests with an
      // error so their callers do not hang on a dead worker.
      qWarning() << "Worker restart FAILED for id:" << id;
      failPendingRequests( pending );
      return;
    }
    qInfo() << "Successfully auto-healed and restarted worker process id:" << id;
    emit workerRestarted( id );

    if ( !pending.empty() )
    {
      // Shared ownership between the replay path and the watchdog timer.
      auto sharedPending = std::make_shared<std::vector<PythonIpcServer::PendingRequest>>( std::move( pending ) );

      // State recovery: replay lost requests once the fresh worker connects.
      // Each replay consumes one retry; a request whose budget is exhausted is
      // answered with an error so the caller never hangs on a dead worker.
      const auto replay = [this, node, sharedPending]() {
        for ( const PythonIpcServer::PendingRequest &req : *sharedPending )
        {
          if ( req.retriesLeft > 0 )
          {
            node->server->sendRequest( req.method, req.params, req.callback, req.retriesLeft - 1 );
          }
          else
          {
            QJsonObject err;
            err[QStringLiteral( "message" )] = QStringLiteral( "Worker crashed; retries exhausted" );
            req.callback( err, true );
          }
        }
        sharedPending->clear();
      };
      if ( node->server->hasClient() )
        replay();
      else
        connect( node->server, &PythonIpcServer::clientConnected, this, [replay]() { replay(); } );

      // Watchdog: if the restarted worker never connects (dies during
      // startup), fail the pending callbacks instead of leaking them.
      auto *watchdog = new QTimer( this );
      watchdog->setSingleShot( true );
      connect( watchdog, &QTimer::timeout, this, [sharedPending]() {
        if ( sharedPending->empty() )
          return;
        QJsonObject err;
        err[QStringLiteral( "message" )] = QStringLiteral( "Worker restart timed out" );
        for ( const PythonIpcServer::PendingRequest &req : *sharedPending )
          req.callback( err, true );
        sharedPending->clear();
      } );
      watchdog->start( 5000 );
    }
  }
  else
  {
    node->isRestarting = false;
    qWarning() << "Worker restart listen failed for id:" << id;
    failPendingRequests( pending );
  }
  } );
}

void PythonWorkerProcessPool::failPendingRequests(
  const std::vector<PythonIpcServer::PendingRequest> &pending )
{
  for ( const PythonIpcServer::PendingRequest &req : pending )
  {
    QJsonObject err;
    err[QStringLiteral( "message" )] = QStringLiteral( "Worker crashed; retries exhausted" );
    req.callback( err, true );
  }
}

} // namespace sicnu::python::isolated

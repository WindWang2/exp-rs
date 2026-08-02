// src/python/isolated/python_worker_process_pool.cpp
#include "python_worker_process_pool.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

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
  for ( WorkerNode *node : m_nodes )
  {
    if ( node && !node->isBusy && node->worker && node->worker->isRunning() && node->server && node->server->hasClient() )
    {
      node->isBusy = true;
      return node;
    }
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

  // Self-healing auto-restart with unique socket name
  node->restartCount++;
  QString socketName = QString( "sicnu_pool_%1_%2_%3" )
                         .arg( QCoreApplication::applicationPid() )
                         .arg( id )
                         .arg( node->restartCount );
  node->server = new PythonIpcServer();
  node->worker = new PythonWorkerProcess();
  node->isBusy = false;

  if ( node->server->listen( socketName ) )
  {
    connect( node->worker, &PythonWorkerProcess::workerCrashed, this, [this, node]() {
      handleWorkerCrash( node );
    } );
    node->worker->startWorker( socketName, m_pythonPath, m_scriptPath );
    node->isRestarting = false;
    qInfo() << "Successfully auto-healed and restarted worker process id:" << id;
    emit workerRestarted( id );
  }
  else
  {
    node->isRestarting = false;
  }
}

} // namespace sicnu::python::isolated

// src/python/isolated/python_worker_process_pool.h
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include "python_ipc_server.h"
#include "python_worker_process.h"

namespace sicnu::python::isolated
{

struct WorkerNode
{
  int id = 0;
  int restartCount = 0;
  PythonWorkerProcess *worker = nullptr;
  PythonIpcServer *server = nullptr;
  bool isBusy = false;
  bool isRestarting = false;
};

/// Composite health snapshot for the worker pool (ADR 0045).
struct PoolHealthSnapshot
{
  int total = 0;           ///< Total nodes in the pool
  int active = 0;          ///< Nodes with a running worker process
  int available = 0;       ///< Active nodes that are not busy
  int totalRestarts = 0;   ///< Cumulative crash restarts since pool init
  bool isHealthy() const { return active > 0 && available > 0; }
};

class PythonWorkerProcessPool : public QObject
{
  Q_OBJECT

  public:
    explicit PythonWorkerProcessPool( int poolSize = 2, QObject *parent = nullptr );
    ~PythonWorkerProcessPool() override;

    bool initialize( const QString &pythonPath = QString(), const QString &scriptPath = QString() );
    void shutdown();

    WorkerNode *acquireWorker();
    void releaseWorker( WorkerNode *node );

    int poolSize() const { return m_poolSize; }
    bool setPoolSize( int newSize );
    int activeWorkerCount() const;
    int availableWorkerCount() const;

    /// Single deep-module query returning a composite pool health snapshot (ADR 0045).
    PoolHealthSnapshot poolHealth() const;

  signals:
    void workerCrashed( int workerId, const QString &reason );
    void workerRestarted( int workerId );

  private:
    WorkerNode *createWorkerNode( int id );
    void handleWorkerCrash( WorkerNode *node );

    int m_poolSize = 2;
    int m_nextWorkerId = 1;
    QString m_pythonPath;
    QString m_scriptPath;
    QList<WorkerNode *> m_nodes;
};

} // namespace sicnu::python::isolated

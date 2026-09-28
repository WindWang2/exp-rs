// src/python/isolated/python_worker_process_pool.h
#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

#include "python_ipc_server.h"
#include "python_worker_process.h"

#include <map>
#include <memory>
#include <vector>

namespace sicnu::python::isolated
{

/// Crash-restart budget per worker node. A node whose worker exhausts it is
/// retired (no further auto-restarts — tight crash-loop protection); workers
/// that keep serving full job cycles earn the budget back one step per
/// released job (releaseWorker), so a long-lived pool never permanently loses
/// capacity to a transient bad patch.
constexpr int kMaxWorkerCrashRestarts = 5;

struct WorkerNode
{
  int id = 0;
  int restartCount = 0;      ///< cumulative crash restarts since creation (health metric)
  int crashBudgetLeft = kMaxWorkerCrashRestarts;
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

/// THREADING & RE-ENTRANCY CONTRACT: every WorkerNode / m_pendingRecovery /
/// recovery-timer access happens on the pool's HOME thread (signals are
/// direct connections on it; sendRequestSync marshals foreign threads onto
/// it; the backoff/watchdog timers fire there). Recovery callbacks
/// (failPendingRequests / replay) MUST NOT spin a nested event loop or
/// re-enter pool APIs — shutdown() answers recovered requests after moving
/// them OUT of the map precisely so a re-entrant callback cannot invalidate
/// the bookkeeping, and the backoff timer bails on m_shuttingDown.

/// Re-dispatches ONE recovered (worker died mid-request) request onto the
/// restarted worker's server, consuming one retry budget step. A request
/// with no budget left — or one the server cannot accept (no client attached,
/// socket already gone: sendRequest would silently return -1) — is answered
/// with a typed error carrying @p failureMessage instead, so a recovered
/// caller never hangs on a dead worker. Returns true iff the request was
/// actually re-sent. Exposed for the recovery-path oracles.
bool replayRecoveredRequest( PythonIpcServer *server,
                             const PythonIpcServer::PendingRequest &req,
                             const QString &failureMessage );

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
    /// Emitted whenever a node loses its worker — crash, clean self-exit with
    /// in-flight work, or protocol EOF — carrying the loss classification
    /// (real exit code / stderr tail for crashes).
    void workerCrashed( int workerId, const QString &reason );
    void workerRestarted( int workerId );

  private:
    WorkerNode *createWorkerNode( int id );
    /// Crash entry: builds the classification reason (exit code + crash axis
    /// + captured stderr tail) from the dying worker, then hands over to
    /// handleWorkerLoss for the actual recovery.
    void handleWorkerCrash( WorkerNode *node );
    /// Shared recovery for every way a worker can be lost while requests are
    /// in flight (crash death, clean exit with unanswered work, socket EOF):
    /// owns the pending requests, restarts the worker after backoff and
    /// replays. A clean exit WITHOUT in-flight work never enters here — that
    /// is a worker retiring, not a channel failure.
    void handleWorkerLoss( WorkerNode *node, const QString &reason );
    void bindNodeSignals( WorkerNode *node );
    WorkerNode *findNodeById( int id ) const;
    /// Answers every recovered request with an explicit, per-cause typed
    /// error so callers never wait forever on a dead worker.
    void failPendingRequests( const std::vector<PythonIpcServer::PendingRequest> &pending,
                              const QString &message );

    int m_poolSize = 2;
    int m_nextWorkerId = 1;
    bool m_shuttingDown = false;
    QString m_pythonPath;
    QString m_scriptPath;
    QList<WorkerNode *> m_nodes;
    /// Recovered requests awaiting their restart-backoff timer / replay /
    /// watchdog, keyed by node id. Sole ownership lives HERE (not in the
    /// timer lambda's capture): a pool destroyed mid-backoff answers its
    /// survivors from shutdown() instead of leaking them uninvoked.
    std::map<int, std::shared_ptr<std::vector<PythonIpcServer::PendingRequest>>> m_pendingRecovery;
};

} // namespace sicnu::python::isolated

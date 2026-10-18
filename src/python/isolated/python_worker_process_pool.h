// src/python/isolated/python_worker_process_pool.h
#pragma once

#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>

#include "python_ipc_server.h"
#include "python_worker_process.h"

#include <atomic>
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
  std::atomic<int> restartCount{ 0 };      ///< cumulative crash restarts since creation (health metric)
  std::atomic<int> crashBudgetLeft{ kMaxWorkerCrashRestarts };
  PythonWorkerProcess *worker = nullptr;
  PythonIpcServer *server = nullptr;
  std::atomic<bool> isBusy{ false };
  std::atomic<bool> isRestarting{ false };
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

    /// Blocks up to @p timeoutMs for a free node, marking it busy
    /// atomically (scan-and-mark under the node mutex — #1462: the old
    /// unsynchronized check-then-mark let a re-entrant or cross-thread
    /// acquire double-occupy one node). On the pool's own thread the wait
    /// is event-loop driven (queued releases must be able to land); other
    /// threads poll with a plain sleep. Returns nullptr on timeout.
    WorkerNode *acquireWorker( int timeoutMs = 1000 );
    void releaseWorker( WorkerNode *node );

    int poolSize() const { return m_poolSize; }
    bool setPoolSize( int newSize );
    int activeWorkerCount() const;
    int availableWorkerCount() const;

    /// Single deep-module query returning a composite pool health snapshot (ADR 0045).
    PoolHealthSnapshot poolHealth() const;

    /// Thread-safe lookup of a worker's active IPC server (DEF-RNT-01).
    PythonIpcServer *serverForWorker( int id ) const;

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
    /// @p worker / @p server are the objects the caller installed under
    /// m_nodesMutex; pass them on the restart path so the binding never
    /// re-reads node->worker / node->server unlocked while a concurrent
    /// handleWorkerLoss() can replace them (#1486). The defaults keep the
    /// creation path (which owns the node exclusively) unchanged.
    void bindNodeSignals( WorkerNode *node, PythonWorkerProcess *worker = nullptr,
                          PythonIpcServer *server = nullptr );
    /// Arms the one-shot connect-handshake watchdog for a freshly (re)spawned
    /// worker: no client attached within the window means the node is treated
    /// as lost (restart-or-retire) instead of parking in acquire-unavailable
    /// limbo while still counting as available. @p server is the server the
    /// caller installed under the lock (#1486).
    void armHandshakeWatchdog( WorkerNode *node, PythonIpcServer *server = nullptr );
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
    /// Guards the m_nodes LIST structure and the WorkerNode::isBusy
    /// check-and-mark in acquireWorker/releaseWorker (#1462), as well as
    /// WorkerNode::worker and WorkerNode::server pointer mutation during
    /// worker loss and recovery (#1486).
    /// NEVER hold this across signal emission or pending-request callbacks:
    /// those may re-enter the pool.
    mutable QMutex m_nodesMutex;
    /// Recovered requests awaiting their restart-backoff timer / replay /
    /// watchdog, keyed by node id. Sole ownership lives HERE (not in the
    /// timer lambda's capture): a pool destroyed mid-backoff answers its
    /// survivors from shutdown() instead of leaking them uninvoked.
    std::map<int, std::shared_ptr<std::vector<PythonIpcServer::PendingRequest>>> m_pendingRecovery;
};

} // namespace sicnu::python::isolated

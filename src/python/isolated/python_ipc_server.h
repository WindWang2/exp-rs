// src/python/isolated/python_ipc_server.h
#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QMutex>
#include <functional>
#include <unordered_map>
#include <vector>

namespace sicnu::python::isolated
{

/// Outcome of a correlated sendRequestAndAwait round trip.
enum class AwaitStatus
{
  Ok,           ///< Response received (check isError for JSON-RPC errors)
  NoClient,     ///< No worker connected; nothing was sent
  Timeout,      ///< timeoutMs elapsed without a response
  Disconnected, ///< Worker disconnected while awaiting the response
  Cancelled,    ///< cancelPredicate returned true while awaiting (#649)
};

class PythonIpcServer : public QObject
{
  Q_OBJECT

  public:
    /// A request that has been sent but whose response has not arrived yet.
    /// The pool uses these to re-dispatch work lost to a worker crash
    /// (ADR 0064 state recovery).
    struct PendingRequest
    {
      int id = 0;                                                                 ///< request id on the originating server
      QString method;                                                             ///< RPC method name
      QJsonObject params;                                                         ///< RPC parameters
      std::function<void( const QJsonObject &result, bool isError )> callback;    ///< original caller callback
      int retriesLeft = 1;                                                        ///< remaining re-dispatch attempts
    };

    explicit PythonIpcServer( QObject *parent = nullptr );
    ~PythonIpcServer() override;

    bool listen( const QString &serverName );
    void close();
    bool isListening() const;
    bool hasClient() const;
    QString serverName() const;

    /**
     * Send an async JSON-RPC request. The request is recorded as in-flight so
     * that a worker crash can re-dispatch it (see takeInFlightRequests()).
     *
     * @param retriesLeft re-dispatch budget: the pool decrements this each time
     *                    it replays the request on a restarted worker; 0 means
     *                    the request is answered with an error instead of being
     *                    sent again.
     * @return the correlated request id, or -1 when NOTHING was sent and the
     *         callback was NOT registered (no connected client) — recovery
     *         paths must answer the caller instead of waiting for a response
     *         that can never arrive.
     */
    int sendRequest( const QString &method, const QJsonObject &params,
                     std::function<void( const QJsonObject &result, bool isError )> callback = nullptr,
                     int retriesLeft = 1 );

    /// Sends a request and blocks the calling thread in a nested event loop
    /// until the correlated response arrives, the timeout elapses, or the
    /// client disconnects. On AwaitStatus::Ok, result/isError carry the
    /// response payload. Main-thread only (mirrors the rest of this class).
    AwaitStatus sendRequestAndAwait( const QString &method, const QJsonObject &params,
                                     QJsonObject &result, bool &isError, int timeoutMs );

    /// Synchronous request/response, safe from any thread including JobEngine
    /// worker threads. Main thread: QEventLoop-based (sendRequestAndAwait).
    /// Worker threads (#649): only the send is marshalled to the home thread;
    /// the caller blocks on a QWaitCondition (no nested event loop anywhere,
    /// no GUI-thread re-entrancy) and cancellation is polled in <=250 ms
    /// slices. Incoming JSON-RPC requests (e.g. iface.get_active_layer)
    /// received while waiting are queued and re-emitted via messageReceived on
    /// the server's home thread after the call returns, so they are not lost.
    /// @param cancelPredicate polled (<= every 250 ms) while the caller's
    /// thread waits; when it fires the wait returns Cancelled and the
    /// in-flight request is retired (#649).
    AwaitStatus sendRequestSync( const QString &method, const QJsonObject &params,
                                 QJsonObject &result, bool &isError, int timeoutMs,
                                 const std::function<bool()> &cancelPredicate = {} );

    void sendResponse( int id, const QJsonObject &result );
    void sendError( int id, const QString &errorMessage );

    /**
     * Take ownership of every request currently in flight (sent, not yet
     * answered) together with their callbacks and retry budgets, and clear the
     * internal tracking. Called by the worker pool when a worker dies so the
     * requests can be re-dispatched to a restarted worker. Idempotent: a
     * second call returns an empty vector.
     */
    std::vector<PendingRequest> takeInFlightRequests();

    /// Retires ONE in-flight request the caller gave up on (e.g. a load
    /// that timed out, #1384): it is removed from crash-recovery replay
    /// AND from the response dispatch, so neither a restarted worker nor a
    /// late answer can act on it. Returns true when something was retired.
    bool cancelInFlight( int id );

    /// Number of requests currently in flight (sent, not yet answered).
    /// The pool reads this to distinguish a worker retiring cleanly from one
    /// that disappeared with unanswered work.
    int inFlightCount() const { return static_cast<int>( m_inFlight.size() ); }

    /// Parses any bytes still buffered on the (possibly half-closed) socket.
    /// Called by the pool BEFORE takeInFlightRequests(): a worker that wrote
    /// its final answer and died in the same instant leaves the answer in the
    /// receive buffer, and the process-death and data-ready notifications are
    /// separate notifiers with NO ordering guarantee — without this drain the
    /// recovery path tears down the callback before the answer ever parses
    /// and the caller is told the worker died on a request it actually
    /// completed.
    void drainBufferedResponses();

  signals:
    void clientConnected();
    void clientDisconnected();
    void messageReceived( const QJsonObject &message );

  private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

  private:
    int sendRequestInternal( const QString &method, const QJsonObject &params,
                             std::function<void( const QJsonObject &result, bool isError )> callback,
                             int retriesLeft, bool trackInFlight );
    void dropInFlight( int id );

    QLocalServer *m_server = nullptr;
    QLocalSocket *m_socket = nullptr;
    QByteArray m_buffer;
    int m_nextRequestId = 1;
    QMutex m_requestIdMutex; ///< guards m_nextRequestId across threads
    std::unordered_map<int, std::function<void( const QJsonObject &, bool )>> m_callbacks;
    std::vector<PendingRequest> m_inFlight; ///< tracked requests awaiting a response
};

} // namespace sicnu::python::isolated

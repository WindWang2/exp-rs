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
     */
    void sendRequest( const QString &method, const QJsonObject &params,
                      std::function<void( const QJsonObject &result, bool isError )> callback = nullptr,
                      int retriesLeft = 1 );

    /// Sends a request and blocks the calling thread in a nested event loop
    /// until the correlated response arrives, the timeout elapses, or the
    /// client disconnects. On AwaitStatus::Ok, result/isError carry the
    /// response payload. Main-thread only (mirrors the rest of this class).
    AwaitStatus sendRequestAndAwait( const QString &method, const QJsonObject &params,
                                     QJsonObject &result, bool &isError, int timeoutMs );

    /// Synchronous request/response using waitForReadyRead (no QEventLoop).
    /// Safe from any thread, including JobEngine worker threads: during the
    /// wait the readyRead signal is disconnected so the calling thread owns the
    /// socket exclusively. Incoming JSON-RPC requests (e.g. iface.get_active_layer)
    /// received while waiting are queued and re-emitted via messageReceived on
    /// the server's home thread after the call returns, so they are not lost.
    AwaitStatus sendRequestSync( const QString &method, const QJsonObject &params,
                                 QJsonObject &result, bool &isError, int timeoutMs );

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

  signals:
    void clientConnected();
    void clientDisconnected();
    void messageReceived( const QJsonObject &message );

  private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

  private:
    void sendRequestInternal( const QString &method, const QJsonObject &params,
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

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
    explicit PythonIpcServer( QObject *parent = nullptr );
    ~PythonIpcServer() override;

    bool listen( const QString &serverName );
    void close();
    bool isListening() const;
    bool hasClient() const;
    QString serverName() const;

    void sendRequest( const QString &method, const QJsonObject &params,
                      std::function<void( const QJsonObject &result, bool isError )> callback = nullptr );

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

  signals:
    void clientConnected();
    void clientDisconnected();
    void messageReceived( const QJsonObject &message );

  private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

  private:
  QLocalServer *m_server = nullptr;
  QLocalSocket *m_socket = nullptr;
  QByteArray m_buffer;
  int m_nextRequestId = 1;
  QMutex m_requestIdMutex; ///< guards m_nextRequestId across threads
  std::unordered_map<int, std::function<void( const QJsonObject &, bool )>> m_callbacks;
};

} // namespace sicnu::python::isolated

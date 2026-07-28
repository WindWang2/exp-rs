// src/python/isolated/python_ipc_server.h
#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <functional>

namespace sicnu::python::isolated
{

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
    std::unordered_map<int, std::function<void( const QJsonObject &, bool )>> m_callbacks;
};

} // namespace sicnu::python::isolated

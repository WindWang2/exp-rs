// src/python/isolated/python_ipc_server.cpp
#include "python_ipc_server.h"

#include <QDebug>
#include <QJsonArray>

namespace sicnu::python::isolated
{

PythonIpcServer::PythonIpcServer( QObject *parent )
  : QObject( parent )
{
  m_server = new QLocalServer( this );
  connect( m_server, &QLocalServer::newConnection, this, &PythonIpcServer::onNewConnection );
}

PythonIpcServer::~PythonIpcServer()
{
  close();
}

bool PythonIpcServer::listen( const QString &serverName )
{
  QLocalServer::removeServer( serverName );
  return m_server->listen( serverName );
}

void PythonIpcServer::close()
{
  if ( m_socket )
  {
    m_socket->disconnect();
    m_socket->close();
    m_socket->deleteLater();
    m_socket = nullptr;
  }
  if ( m_server && m_server->isListening() )
  {
    m_server->close();
  }
}

bool PythonIpcServer::isListening() const
{
  return m_server && m_server->isListening();
}

bool PythonIpcServer::hasClient() const
{
  return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

QString PythonIpcServer::serverName() const
{
  return m_server ? m_server->serverName() : QString();
}

void PythonIpcServer::onNewConnection()
{
  if ( m_socket )
  {
    // Close existing socket if new connection arrives
    m_socket->disconnect();
    m_socket->close();
    m_socket->deleteLater();
  }

  m_socket = m_server->nextPendingConnection();
  if ( m_socket )
  {
    connect( m_socket, &QLocalSocket::readyRead, this, &PythonIpcServer::onReadyRead );
    connect( m_socket, &QLocalSocket::disconnected, this, &PythonIpcServer::onSocketDisconnected );
    emit clientConnected();
  }
}

void PythonIpcServer::onSocketDisconnected()
{
  if ( m_socket )
  {
    m_socket->deleteLater();
    m_socket = nullptr;
  }
  emit clientDisconnected();
}

void PythonIpcServer::onReadyRead()
{
  if ( !m_socket )
    return;

  m_buffer.append( m_socket->readAll() );

  while ( true )
  {
    int newlineIdx = m_buffer.indexOf( '\n' );
    if ( newlineIdx == -1 )
      break;

    QByteArray line = m_buffer.left( newlineIdx ).trimmed();
    m_buffer.remove( 0, newlineIdx + 1 );

    if ( line.isEmpty() )
      continue;

    QJsonDocument doc = QJsonDocument::fromJson( line );
    if ( doc.isObject() )
      {
        QJsonObject msg = doc.object();
        if ( msg.contains( QStringLiteral( "id" ) ) && ( msg.contains( QStringLiteral( "result" ) ) || msg.contains( QStringLiteral( "error" ) ) ) )
        {
          int id = msg[QStringLiteral( "id" )].toInt();
          auto it = m_callbacks.find( id );
          if ( it != m_callbacks.end() )
          {
            bool isErr = msg.contains( QStringLiteral( "error" ) );
            QJsonObject res = isErr ? msg[QStringLiteral( "error" )].toObject() : msg[QStringLiteral( "result" )].toObject();
            it->second( res, isErr );
            m_callbacks.erase( it );
          }
        }

        emit messageReceived( msg );
      }
  }
}

void PythonIpcServer::sendRequest( const QString &method, const QJsonObject &params,
                                   std::function<void( const QJsonObject &, bool )> callback )
{
  if ( !m_socket || m_socket->state() != QLocalSocket::ConnectedState )
    return;

  int reqId = m_nextRequestId++;
  if ( callback )
  {
    m_callbacks[reqId] = callback;
  }

  QJsonObject req;
  req[QStringLiteral( "jsonrpc" )] = QStringLiteral( "2.0" );
  req[QStringLiteral( "method" )] = method;
  req[QStringLiteral( "params" )] = params;
  req[QStringLiteral( "id" )] = reqId;

  QByteArray data = QJsonDocument( req ).toJson( QJsonDocument::Compact );
  data.append( '\n' );
  m_socket->write( data );
  m_socket->flush();
}

void PythonIpcServer::sendResponse( int id, const QJsonObject &result )
{
  if ( !m_socket || m_socket->state() != QLocalSocket::ConnectedState )
    return;

  QJsonObject resp;
  resp[QStringLiteral( "jsonrpc" )] = QStringLiteral( "2.0" );
  resp[QStringLiteral( "id" )] = id;
  resp[QStringLiteral( "result" )] = result;

  QByteArray data = QJsonDocument( resp ).toJson( QJsonDocument::Compact );
  data.append( '\n' );
  m_socket->write( data );
  m_socket->flush();
}

void PythonIpcServer::sendError( int id, const QString &errorMessage )
{
  if ( !m_socket || m_socket->state() != QLocalSocket::ConnectedState )
    return;

  QJsonObject errObj;
  errObj[QStringLiteral( "message" )] = errorMessage;

  QJsonObject resp;
  resp[QStringLiteral( "jsonrpc" )] = QStringLiteral( "2.0" );
  resp[QStringLiteral( "id" )] = id;
  resp[QStringLiteral( "error" )] = errObj;

  QByteArray data = QJsonDocument( resp ).toJson( QJsonDocument::Compact );
  data.append( '\n' );
  m_socket->write( data );
  m_socket->flush();
}

} // namespace sicnu::python::isolated

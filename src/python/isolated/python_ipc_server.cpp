// src/python/isolated/python_ipc_server.cpp
#include "python_ipc_server.h"

#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QTimer>

#include <chrono>
#include <memory>

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
  m_server->setSocketOptions( QLocalServer::UserAccessOption );
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

  int reqId;
  {
    QMutexLocker lock( &m_requestIdMutex );
    reqId = m_nextRequestId++;
  }
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

AwaitStatus PythonIpcServer::sendRequestAndAwait( const QString &method, const QJsonObject &params,
                                                  QJsonObject &result, bool &isError, int timeoutMs )
{
  result = QJsonObject();
  isError = false;
  if ( !hasClient() )
  {
    return AwaitStatus::NoClient;
  }

  QEventLoop loop;
  QTimer timeoutTimer;
  timeoutTimer.setSingleShot( true );

  bool responded = false;
  bool disconnected = false;

  QMetaObject::Connection disconnectConn =
    connect( this, &PythonIpcServer::clientDisconnected, &loop, [&disconnected, &loop]() {
      disconnected = true;
      loop.quit();
    } );

  sendRequest( method, params, [&]( const QJsonObject &response, bool responseIsError ) {
    responded = true;
    result = response;
    isError = responseIsError;
    loop.quit();
  } );

  connect( &timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit );
  timeoutTimer.start( timeoutMs );
  loop.exec();

  disconnect( disconnectConn );

  if ( disconnected && !responded )
  {
    return AwaitStatus::Disconnected;
  }
  if ( !responded )
  {
    return AwaitStatus::Timeout;
  }
  return AwaitStatus::Ok;
}

AwaitStatus PythonIpcServer::sendRequestSync( const QString &method, const QJsonObject &params,
                                              QJsonObject &result, bool &isError, int timeoutMs )
{
  result = QJsonObject();
  isError = false;
  if ( !m_socket || m_socket->state() != QLocalSocket::ConnectedState )
  {
    return AwaitStatus::NoClient;
  }

  int reqId;
  {
    QMutexLocker lock( &m_requestIdMutex );
    reqId = m_nextRequestId++;
  }

  QJsonObject req;
  req[QStringLiteral( "jsonrpc" )] = QStringLiteral( "2.0" );
  req[QStringLiteral( "method" )] = method;
  req[QStringLiteral( "params" )] = params;
  req[QStringLiteral( "id" )] = reqId;

  QByteArray data = QJsonDocument( req ).toJson( QJsonDocument::Compact );
  data.append( '\n' );

  // Temporarily disconnect readyRead so this thread exclusively owns the socket
  // during the synchronous wait - the main thread's onReadyRead must not race.
  const bool wasConnected = disconnect( m_socket, &QLocalSocket::readyRead, this, &PythonIpcServer::onReadyRead );

  m_socket->write( data );
  m_socket->flush();

  QByteArray syncBuffer;
  // Carry over any bytes already buffered by a prior onReadyRead that we did
  // not get to process (edge: response arrived between send and disconnect).
  {
    syncBuffer = m_buffer;
    m_buffer.clear();
  }

  bool responded = false;
  bool disconnected = false;
  QList<QJsonObject> pendingIncoming; // non-response frames to re-emit later

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );

  while ( !responded && !disconnected )
  {
    // Parse whatever we have accumulated so far.
    while ( true )
    {
      int newlineIdx = syncBuffer.indexOf( '\n' );
      if ( newlineIdx == -1 )
        break;

      QByteArray line = syncBuffer.left( newlineIdx ).trimmed();
      syncBuffer.remove( 0, newlineIdx + 1 );

      if ( line.isEmpty() )
        continue;

      QJsonDocument doc = QJsonDocument::fromJson( line );
      if ( !doc.isObject() )
        continue;

      QJsonObject msg = doc.object();
      // Is this the response to our request?
      if ( msg.contains( QStringLiteral( "id" ) )
           && msg.value( QStringLiteral( "id" ) ).toInt() == reqId
           && ( msg.contains( QStringLiteral( "result" ) ) || msg.contains( QStringLiteral( "error" ) ) ) )
      {
        isError = msg.contains( QStringLiteral( "error" ) );
        result = isError ? msg[QStringLiteral( "error" )].toObject()
                         : msg[QStringLiteral( "result" )].toObject();
        responded = true;
        break;
      }
      // Otherwise it is an incoming JSON-RPC request/notification from Python
      // (e.g. iface.get_active_layer). Queue it for re-emission after the call.
      pendingIncoming.append( msg );
    }

    if ( responded )
      break;

    if ( m_socket->state() != QLocalSocket::ConnectedState )
    {
      disconnected = true;
      break;
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                             deadline - std::chrono::steady_clock::now() ).count();
    if ( remaining <= 0 )
      break;

    if ( !m_socket->waitForReadyRead( static_cast<int>( remaining ) ) )
    {
      if ( m_socket->state() != QLocalSocket::ConnectedState )
        disconnected = true;
      break;
    }

    syncBuffer.append( m_socket->readAll() );
  }

  // Return any unparsed bytes to m_buffer for the restored onReadyRead.
  if ( !syncBuffer.isEmpty() )
    m_buffer.prepend( syncBuffer );

  if ( wasConnected )
  {
    connect( m_socket, &QLocalSocket::readyRead, this, &PythonIpcServer::onReadyRead );
  }

  // Re-emit incoming JSON-RPC requests that arrived during the sync wait, on
  // the server's home thread, so AppInterfaceBridge::handleIpcMessage sees them.
  if ( !pendingIncoming.isEmpty() )
  {
    // capture via shared_ptr to survive the deferred lambda safely
    auto messages = std::make_shared<QList<QJsonObject>>( std::move( pendingIncoming ) );
    QMetaObject::invokeMethod( this, [this, messages]() {
      for ( const QJsonObject &msg : *messages )
        emit messageReceived( msg );
    }, Qt::QueuedConnection );
  }

  if ( disconnected && !responded )
    return AwaitStatus::Disconnected;
  if ( !responded )
    return AwaitStatus::Timeout;
  return AwaitStatus::Ok;
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

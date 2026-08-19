// src/python/isolated/python_ipc_server.cpp
#include "python_ipc_server.h"

#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

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
  if ( m_socket && m_socket->state() == QLocalSocket::ConnectedState )
  {
    // DATAPY-8: fail-closed — reject second connection while a worker is
    // attached. Otherwise a same-user impostor kills the daemon (DoS) and
    // gains the data./canvas./ui. RPC surface.
    QLocalSocket *pending = m_server->nextPendingConnection();
    if ( pending )
    {
      pending->close();
      pending->deleteLater();
    }
    return;
  }
  if ( m_socket )
  {
    m_socket->disconnect();
    m_socket->close();
    m_socket->deleteLater();
    m_socket = nullptr;
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
          dropInFlight( id );
        }

        emit messageReceived( msg );
      }
  }
}

void PythonIpcServer::sendRequest( const QString &method, const QJsonObject &params,
                                   std::function<void( const QJsonObject &, bool )> callback,
                                   int retriesLeft )
{
  sendRequestInternal( method, params, std::move( callback ), retriesLeft, true );
}

int PythonIpcServer::sendRequestInternal( const QString &method, const QJsonObject &params,
                                          std::function<void( const QJsonObject &, bool )> callback,
                                          int retriesLeft, bool trackInFlight )
{
  if ( !m_socket || m_socket->state() != QLocalSocket::ConnectedState )
    return -1;

  int reqId;
  {
    QMutexLocker lock( &m_requestIdMutex );
    reqId = m_nextRequestId++;
  }
  if ( callback )
  {
    m_callbacks[reqId] = callback;
    if ( trackInFlight )
    {
      PendingRequest p;
      p.id = reqId;
      p.method = method;
      p.params = params;
      p.callback = callback;
      p.retriesLeft = retriesLeft;
      m_inFlight.push_back( std::move( p ) );
    }
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
  return reqId;
}

void PythonIpcServer::dropInFlight( int id )
{
  for ( auto it = m_inFlight.begin(); it != m_inFlight.end(); ++it )
  {
    if ( it->id == id )
    {
      m_inFlight.erase( it );
      return;
    }
  }
}

std::vector<PythonIpcServer::PendingRequest> PythonIpcServer::takeInFlightRequests()
{
  std::vector<PendingRequest> taken;
  taken.swap( m_inFlight );
  for ( const PendingRequest &p : taken )
    m_callbacks.erase( p.id );
  return taken;
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

  bool disconnected = false;

  QMetaObject::Connection disconnectConn =
    connect( this, &PythonIpcServer::clientDisconnected, &loop, [&disconnected, &loop]() {
      disconnected = true;
      loop.quit();
    } );

  struct AwaitContext
  {
    bool responded = false;
    QJsonObject result;
    bool isError = false;
    QPointer<QEventLoop> loop;
  };
  auto ctx = std::make_shared<AwaitContext>();
  ctx->loop = &loop;

  // Blocking path: the callback captures shared context rather than stack locals,
  // and is explicitly erased if timeout/disconnect fires before response.
  const int reqId = sendRequestInternal( method, params, [ctx]( const QJsonObject &response, bool responseIsError ) {
    ctx->responded = true;
    ctx->result = response;
    ctx->isError = responseIsError;
    if ( ctx->loop )
      ctx->loop->quit();
  }, 1, false );

  if ( reqId < 0 )
  {
    disconnect( disconnectConn );
    return AwaitStatus::NoClient;
  }

  connect( &timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit );
  timeoutTimer.start( timeoutMs );
  loop.exec();

  disconnect( disconnectConn );
  ctx->loop = nullptr; // decouple loop before stack frame exits

  if ( !ctx->responded )
  {
    m_callbacks.erase( reqId );
  }

  if ( disconnected && !ctx->responded )
  {
    return AwaitStatus::Disconnected;
  }
  if ( !ctx->responded )
  {
    return AwaitStatus::Timeout;
  }
  result = ctx->result;
  isError = ctx->isError;
  return AwaitStatus::Ok;
}

AwaitStatus PythonIpcServer::sendRequestSync( const QString &method, const QJsonObject &params,
                                              QJsonObject &result, bool &isError, int timeoutMs )
{
  result = QJsonObject();
  isError = false;
  if ( !hasClient() )
  {
    return AwaitStatus::NoClient;
  }

  if ( QThread::currentThread() == thread() )
  {
    return sendRequestAndAwait( method, params, result, isError, timeoutMs );
  }

  // Worker thread: invoke sendRequestAndAwait on server's home thread via shared context
  struct SyncContext
  {
    QMutex mutex;
    QWaitCondition waitCond;
    bool done = false;
    AwaitStatus status = AwaitStatus::NoClient;
    QJsonObject result;
    bool isError = false;
  };
  auto ctx = std::make_shared<SyncContext>();

  QPointer<PythonIpcServer> weakServer( this );
  QMetaObject::invokeMethod( this, [weakServer, method, params, timeoutMs, ctx]() {
    if ( weakServer )
    {
      ctx->status = weakServer->sendRequestAndAwait( method, params, ctx->result, ctx->isError, timeoutMs );
    }
    else
    {
      ctx->status = AwaitStatus::Disconnected;
    }
    QMutexLocker lock( &ctx->mutex );
    ctx->done = true;
    ctx->waitCond.wakeAll();
  }, Qt::QueuedConnection );

  {
    QMutexLocker lock( &ctx->mutex );
    while ( !ctx->done )
    {
      if ( !ctx->waitCond.wait( &ctx->mutex, timeoutMs + 1000 ) )
      {
        return AwaitStatus::Timeout;
      }
    }
  }
  result = ctx->result;
  isError = ctx->isError;
  return ctx->status;
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

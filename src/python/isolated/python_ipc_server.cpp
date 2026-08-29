// src/python/isolated/python_ipc_server.cpp
#include "python_ipc_server.h"

#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
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
            // Erase BEFORE invoking (#624): the callback may re-enter
            // sendRequest (rehash -> iterator invalidation) or call
            // takeInFlightRequests (erasing this id) - erasing afterwards
            // with the stale iterator was UB.
            auto callback = std::move( it->second );
            m_callbacks.erase( it );
            callback( res, isErr );
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
                                              QJsonObject &result, bool &isError, int timeoutMs,
                                              const std::function<bool()> &cancelPredicate )
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

  // Worker thread (#649): only the SEND is marshalled onto the server's home
  // thread (the socket is a home-thread object and the marshalled lambda is
  // quick and non-blocking). The worker then blocks on a QWaitCondition that
  // the response/disconnect handlers wake. The previous implementation queued
  // the whole sendRequestAndAwait onto the home thread, which ran a nested
  // QEventLoop there for up to timeoutMs - a GUI-thread re-entrancy window
  // per py: call, despite the header contract claiming a QEventLoop-free
  // synchronous wait.
  struct SyncContext
  {
    QMutex mutex;
    QWaitCondition waitCond;
    bool done = false;
    bool responded = false;
    bool disconnected = false;
    int reqId = -1; // valid (>= 0) once the home thread has sent the request
    bool cancelled = false;
    QJsonObject result;
    bool isError = false;
    std::function<bool()> cancelPredicate;
  };
  auto ctx = std::make_shared<SyncContext>();
  ctx->cancelPredicate = cancelPredicate;

  QPointer<PythonIpcServer> weakServer( this );

  // Sends on the home thread; wakes the worker when the request could not be
  // sent at all (no client at send time).
  QMetaObject::invokeMethod( this, [weakServer, method, params, ctx]() {
    if ( !weakServer )
    {
      QMutexLocker lock( &ctx->mutex );
      ctx->done = true;
      ctx->disconnected = true;
      ctx->waitCond.wakeAll();
      return;
    }
    const int reqId = weakServer->sendRequestInternal(
      method, params,
      [ctx]( const QJsonObject &response, bool responseIsError ) {
        // Runs on the home thread when the worker's response arrives.
        QMutexLocker lock( &ctx->mutex );
        ctx->responded = true;
        ctx->result = response;
        ctx->isError = responseIsError;
        ctx->done = true;
        ctx->waitCond.wakeAll();
      },
      1, false );
    QMutexLocker lock( &ctx->mutex );
    ctx->reqId = reqId;
    if ( reqId < 0 )
    {
      ctx->done = true;
      ctx->waitCond.wakeAll();
    }
  }, Qt::QueuedConnection );

  // Wake the worker on disconnect as well (otherwise it sits out the full
  // timeout after the client died).
  QMetaObject::Connection disconnectConn =
    connect( this, &PythonIpcServer::clientDisconnected, this, [ctx]() {
      QMutexLocker lock( &ctx->mutex );
      ctx->disconnected = true;
      ctx->done = true;
      ctx->waitCond.wakeAll();
    }, Qt::QueuedConnection );

  bool timedOut = false;
  {
    QMutexLocker lock( &ctx->mutex );
    QElapsedTimer deadline;
    deadline.start();
    while ( !ctx->done )
    {
      // Poll the cancellation predicate in bounded slices so a cancel wakes
      // this wait within ~250 ms instead of sitting out the full timeout
      // (spurious wakeups are handled by the while loop).
      if ( ctx->cancelPredicate && ctx->cancelPredicate() )
      {
        ctx->cancelled = true;
        ctx->done = true;
        break;
      }
      const qint64 remaining = timeoutMs - deadline.elapsed();
      if ( remaining <= 0 )
      {
        timedOut = true;
        break;
      }
      if ( !ctx->waitCond.wait( &ctx->mutex, std::min<qint64>( remaining, 250 ) ) )
        continue; // re-check done/cancel/timeout under the lock
    }
  }
  disconnect( disconnectConn );

  const bool eraseCallback = !ctx->responded && ctx->reqId >= 0;
  const int reqId = ctx->reqId;
  if ( eraseCallback )
  {
    // m_callbacks lives on the home thread; marshal the erase (fire-and-
    // forget - a late response then resolves into an abandoned context).
    QMetaObject::invokeMethod( this, [this, reqId]() {
      m_callbacks.erase( reqId );
    }, Qt::QueuedConnection );
  }

  AwaitStatus status = AwaitStatus::Ok;
  if ( ctx->responded )
  {
    result = ctx->result;
    isError = ctx->isError;
    status = AwaitStatus::Ok;
  }
  else if ( ctx->cancelled )
  {
    status = AwaitStatus::Cancelled;
  }
  else if ( timedOut )
  {
    status = AwaitStatus::Timeout;
  }
  else
  {
    status = AwaitStatus::Disconnected;
  }
  return status;
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

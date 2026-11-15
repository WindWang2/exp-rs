#include "pi_conversation_runtime.h"

#include "pi_process.h"

#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>

#include <utility>

namespace sicnu::agent::runtime
{
namespace
{
QString safeFailure()
{
  return QCoreApplication::translate( "sicnu::agent::runtime::PiConversationRuntime", "The Pi conversation could not continue." );
}

bool identity( const QString &value )
{
  if ( value.isEmpty() || value.toUtf8().size() > 256 ) return false;
  for ( const QChar c : value ) if ( c.unicode() < 0x20 || c.unicode() == 0x7f ) return false;
  return true;
}

bool snapshot( const QJsonArray &input, QJsonArray &output, QHash<QString, QString> &mapping )
{
  if ( input.size() > 256 ) return false;
  static const QRegularExpression wireName( QStringLiteral( "^[A-Za-z0-9_-]{1,64}$" ) );
  const QSet<QString> reserved{ QStringLiteral( "codemode" ), QStringLiteral( "tool_search" ), QStringLiteral( "read" ),
    QStringLiteral( "bash" ), QStringLiteral( "powershell" ), QStringLiteral( "edit" ), QStringLiteral( "write" ),
    QStringLiteral( "grep" ), QStringLiteral( "find" ), QStringLiteral( "ls" ) };
  QSet<QString> canonicalIds;
  for ( const auto &item : input )
  {
    auto tool = item.toObject();
    const QString name = tool.value( QStringLiteral( "name" ) ).toString();
    const QString canonical = tool.value( QStringLiteral( "canonicalId" ) ).toString();
    if ( tool.size() != 4 || !wireName.match( name ).hasMatch() || !identity( canonical )
         || mapping.contains( name ) || canonicalIds.contains( canonical ) || reserved.contains( name )
         || name.startsWith( QLatin1String( "mcp__" ) ) || !tool.value( QStringLiteral( "description" ) ).isString()
         || !tool.value( QStringLiteral( "parameters" ) ).isObject()
         || tool.value( QStringLiteral( "parameters" ) ).toObject().value( QStringLiteral( "type" ) ).toString() != QLatin1String( "object" ) )
      return false;
    mapping.insert( name, canonical );
    canonicalIds.insert( canonical );
    output.append( tool );
  }
  return true;
}
} // namespace

PiConversationRuntime::PiConversationRuntime( const QString &absolutePrefixPath, QObject *parent )
  : AgentConversationRuntime( parent ), m_prefixPath( absolutePrefixPath ), m_process( new PiProcess( this ) )
{
  connect( m_process, &PiProcess::ready, this, [this]( const QJsonObject & ) {
    if ( m_closing ) return;
    m_ready = true;
    emit ready();
  } );
  connect( m_process, &PiProcess::eventReceived, this, &PiConversationRuntime::receiveEvent );
  connect( m_process, &PiProcess::failed, this, [this]( const QString &code, const QString & ) {
    m_ready = false;
    if ( m_sessionFailureReported ) return;
    m_sessionFailureReported = true;
    const bool active = !m_runId.isEmpty();
    const QPointer<PiConversationRuntime> alive( this );
    requestHostCancellation();
    if ( !alive ) return;
    if ( active )
    {
      if ( code == QLatin1String( "protocol_tool_mismatch" ) || code == QLatin1String( "protocol_call_mismatch" ) )
        emit toolCallRefused( m_runId, { { QStringLiteral( "reason" ), QStringLiteral( "invalid_runtime_tool_call" ) } } );
      if ( alive ) finishRun( true, safeFailure() );
    }
    else emit settled( {}, true, safeFailure() );
  } );
  connect( m_process, &PiProcess::closed, this, [this] {
    m_ready = false;
    const QPointer<PiConversationRuntime> alive( this );
    requestHostCancellation();
    if ( !alive ) return;
    finishRun( true, {} );
    if ( alive ) emit statusChanged( {}, QStringLiteral( "closed" ) );
  } );
}

PiConversationRuntime::~PiConversationRuntime()
{
  // The host closes its conversation explicitly before destruction. Suppress
  // outward callbacks during teardown; PiProcess still reaps its owned tree.
  m_process->disconnect( this );
  m_process->close();
}

void PiConversationRuntime::refuseSession( const QString &code )
{
  m_ready = false;
  const QPointer<PiConversationRuntime> alive( this );
  emit statusChanged( {}, code );
  if ( alive ) emit settled( {}, true, safeFailure() );
}

void PiConversationRuntime::startSession( const ConversationConfig &config )
{
  if ( m_process->state() != PiProcess::State::Idle && m_process->state() != PiProcess::State::Closed )
  {
    emit statusChanged( m_runId, QStringLiteral( "session_busy" ) );
    return;
  }
  QJsonArray tools;
  QHash<QString, QString> mapping;
  if ( !QDir::isAbsolutePath( m_prefixPath ) || m_prefixPath.startsWith( QLatin1String( "//" ) )
       || m_prefixPath.startsWith( QLatin1String( "\\\\" ) ) )
  { refuseSession( QStringLiteral( "invalid_runtime_prefix" ) ); return; }
  if ( !snapshot( config.tools, tools, mapping ) )
  { refuseSession( QStringLiteral( "invalid_tool_catalog" ) ); return; }
  if ( !identity( config.toolRevision ) )
  { refuseSession( QStringLiteral( "invalid_tool_revision" ) ); return; }

  m_canonicalByProviderName = std::move( mapping );
  m_toolRevision = config.toolRevision;
  m_runId.clear();
  m_pendingCalls.clear();
  m_ready = m_closing = m_cancelRequested = m_responseAfterUsage = m_sessionFailureReported = false;
  PiProcess::Config processConfig;
  processConfig.prefixPath = m_prefixPath;
  processConfig.sessionId = config.sessionId.isEmpty() ? QUuid::createUuid().toString( QUuid::WithoutBraces ) : config.sessionId;
  processConfig.generation = QUuid::createUuid().toString( QUuid::WithoutBraces );
  processConfig.toolRevision = m_toolRevision;
  processConfig.systemPrompt = config.systemPrompt;
  processConfig.tools = tools;
  processConfig.profile = { { QStringLiteral( "baseUrl" ), config.profile.baseUrl },
    { QStringLiteral( "model" ), config.profile.modelName }, { QStringLiteral( "contextWindow" ), 32768 }, { QStringLiteral( "maxTokens" ), 4096 } };
  processConfig.apiKey = config.profile.apiKey.toUtf8();
  m_process->start( processConfig );
  processConfig.apiKey.fill( '\0' );
}

bool PiConversationRuntime::isReady() const
{
  return m_ready && !m_closing && m_runId.isEmpty() && m_process->state() == PiProcess::State::Ready;
}

bool PiConversationRuntime::submitPrompt( const QString &runId, const QString &text )
{
  if ( !isReady() || !identity( runId ) ) return false;
  m_runId = runId;
  m_ready = false;
  m_cancelRequested = m_responseAfterUsage = false;
  m_pendingCalls.clear();
  const QPointer<PiConversationRuntime> alive( this );
  if ( !m_process->sendPrompt( runId, text ) )
  {
    if ( alive && m_runId == runId )
    {
      m_runId.clear();
      m_ready = m_process->state() == PiProcess::State::Ready;
    }
    return false;
  }
  emit responseStarted( runId );
  return true;
}

bool PiConversationRuntime::completeTool( const QString &runId, const QString &callId, const QJsonObject &result )
{
  if ( runId != m_runId || !m_pendingCalls.contains( callId ) ) return false;
  if ( !m_process->completeTool( runId, callId, result ) ) return false;
  m_pendingCalls.remove( callId );
  return true;
}

void PiConversationRuntime::requestHostCancellation()
{
  if ( m_runId.isEmpty() || m_cancelRequested ) return;
  m_cancelRequested = true;
  emit cancelOwnedToolsRequested( m_runId );
}

void PiConversationRuntime::cancelRun()
{
  if ( m_runId.isEmpty() )
  {
    if ( m_closing ) return;
    m_sessionFailureReported = true;
    const QPointer<PiConversationRuntime> alive( this );
    closeSession();
    if ( alive ) emit settled( {}, true, {} );
    return;
  }
  m_process->cancel();
  requestHostCancellation();
}

void PiConversationRuntime::closeSession()
{
  if ( m_closing ) return;
  m_closing = true;
  m_ready = false;
  const QPointer<PiConversationRuntime> alive( this );
  if ( !m_runId.isEmpty() ) m_process->cancel();
  requestHostCancellation();
  if ( alive ) m_process->close();
}

void PiConversationRuntime::finishRun( bool aborted, const QString &error )
{
  if ( m_runId.isEmpty() ) return;
  const QString run = std::exchange( m_runId, {} );
  m_pendingCalls.clear();
  m_responseAfterUsage = false;
  m_ready = !m_closing && !m_sessionFailureReported && m_process->state() == PiProcess::State::Ready;
  emit settled( run, aborted, error );
}

bool PiConversationRuntime::beginResponse( const QString &runId )
{
  if ( !m_responseAfterUsage ) return true;
  m_responseAfterUsage = false;
  const QPointer<PiConversationRuntime> alive( this );
  emit responseStarted( runId );
  return alive && m_runId == runId && !m_closing;
}

void PiConversationRuntime::receiveEvent( const QJsonObject &event )
{
  const QString type = event.value( QStringLiteral( "type" ) ).toString();
  const QString run = event.value( QStringLiteral( "runId" ) ).toString();
  if ( run.isEmpty() || run != m_runId ) return;
  if ( type == QLatin1String( "settled" ) )
  {
    const bool failed = event.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "failed" );
    const QPointer<PiConversationRuntime> alive( this );
    if ( failed ) requestHostCancellation();
    if ( alive ) finishRun( event.value( QStringLiteral( "aborted" ) ).toBool() || failed, failed ? safeFailure() : QString() );
  }
  else if ( type == QLatin1String( "tool.cancel" ) ) requestHostCancellation();
  else if ( !m_closing && ( type == QLatin1String( "content" ) || type == QLatin1String( "reasoning" ) ) )
  {
    if ( !beginResponse( run ) ) return;
    if ( type == QLatin1String( "content" ) ) emit contentDelta( run, event.value( QStringLiteral( "text" ) ).toString() );
    else emit reasoningDelta( run, event.value( QStringLiteral( "text" ) ).toString() );
  }
  else if ( !m_closing && type == QLatin1String( "tool.request" ) )
  {
    const QString name = event.value( QStringLiteral( "name" ) ).toString();
    const QString canonical = m_canonicalByProviderName.value( name );
    const QString call = event.value( QStringLiteral( "callId" ) ).toString();
    if ( canonical.isEmpty() || canonical != event.value( QStringLiteral( "canonicalId" ) ).toString()
         || event.value( QStringLiteral( "toolRevision" ) ).toString() != m_toolRevision || m_pendingCalls.contains( call ) )
    {
      const QPointer<PiConversationRuntime> alive( this );
      emit toolCallRefused( run, { { QStringLiteral( "reason" ), QStringLiteral( "invalid_runtime_tool_call" ) } } );
      if ( alive ) closeSession();
      return;
    }
    m_pendingCalls.insert( call );
    if ( !beginResponse( run ) ) return;
    emit toolCallRequested( run, { { QStringLiteral( "id" ), call }, { QStringLiteral( "type" ), QStringLiteral( "function" ) },
      { QStringLiteral( "function" ), QJsonObject{ { QStringLiteral( "name" ), canonical },
        { QStringLiteral( "arguments" ), QString::fromUtf8( QJsonDocument( event.value( QStringLiteral( "arguments" ) ).toObject() ).toJson( QJsonDocument::Compact ) ) } } } }, m_toolRevision );
  }
  else if ( type == QLatin1String( "usage" ) )
  {
    m_responseAfterUsage = true;
    emit usageUpdated( run, { { QStringLiteral( "usage" ), event.value( QStringLiteral( "usage" ) ) },
      { QStringLiteral( "cumulativeUsage" ), event.value( QStringLiteral( "cumulativeUsage" ) ) } } );
  }
  else if ( type == QLatin1String( "compaction" ) )
    emit statusChanged( run, QStringLiteral( "compaction_" ) + event.value( QStringLiteral( "stage" ) ).toString() );
}
} // namespace sicnu::agent::runtime

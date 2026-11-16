#include "native_conversation_runtime.h"

#include "agent/llm_streaming_client.h"
#include "runtime/observability/trace.h"

#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

namespace sicnu::agent::runtime
{
namespace
{
constexpr int kMaxModelRequests = 16;
constexpr int kMaxToolAttempts = 32;
constexpr int kMaxPendingTools = 16;
constexpr qsizetype kMaxToolResultBytes = 512 * 1024;

QJsonObject toolError( const QString &code )
{
  return { { QStringLiteral( "status" ), QStringLiteral( "error" ) },
           { QStringLiteral( "errorCode" ), code },
           { QStringLiteral( "errorMessage" ), code } };
}
}

NativeConversationRuntime::NativeConversationRuntime( QObject *parent )
  : AgentConversationRuntime( parent ), m_client( new LlmStreamingClient( this ) )
{
  connect( m_client, &LlmStreamingClient::usageReceived, this, [this]( const QJsonObject &usage ) {
    if ( !m_active || !m_transportActive ) return;
    const QString run = m_runId;
    emit usageUpdated( run, { { QStringLiteral( "usage" ), usage } } );
  } );
  connect( m_client, &LlmStreamingClient::contentTokenReceived, this, [this]( const QString &text ) {
    if ( !m_active || !m_transportActive ) return;
    m_content += text;
    emit contentDelta( m_runId, text );
  } );
  connect( m_client, &LlmStreamingClient::reasoningTokenReceived, this, [this]( const QString &text ) {
    if ( !m_active || !m_transportActive ) return;
    m_reasoning += text;
    emit reasoningDelta( m_runId, text );
  } );
  connect( m_client, &LlmStreamingClient::toolCallParsed, this, &NativeConversationRuntime::collectToolCall );
  connect( m_client, &LlmStreamingClient::malformedToolCall, this, &NativeConversationRuntime::collectRefusal );
  connect( m_client, &LlmStreamingClient::errorOccurred, this, [this]( const QString & ) {
    if ( !m_active || !m_transportActive ) return;
    // Provider strings may contain a full URL, response body or echoed secret.
    // Report a controlled diagnostic; never copy them into UI/session history.
    m_responseError = tr( "Native model request failed." );
  } );
  connect( m_client, &LlmStreamingClient::finished, this, [this] {
    if ( !m_active || !m_transportActive ) return;
    m_transportActive = false;
    const quint64 epoch = m_epoch;
    // Exit the transport's signal/parse stack before dispatching even a
    // synchronous interaction. Its old finished/error cannot finish a new run.
    QTimer::singleShot( 0, this, [this, epoch] {
      if ( m_active && m_epoch == epoch ) finishResponse();
    } );
  } );
}

NativeConversationRuntime::~NativeConversationRuntime()
{
  // The owner closes/cancels explicitly while it can still receive signals.
  ++m_epoch;
  m_active = false;
  m_client->cancel();
}

void NativeConversationRuntime::startSession( const ConversationConfig &config )
{
  QPointer<NativeConversationRuntime> alive( this );
  closeSession();
  if ( !alive ) return;
  m_config = config;
  QSet<QString> canonicalIds;
  const QRegularExpression wireName( QStringLiteral( "^[A-Za-z0-9_-]{1,64}$" ) );
  bool valid = !config.sessionId.isEmpty() && config.tools.size() <= 256;
  for ( const auto &value : config.tools )
  {
    const QJsonObject tool = value.toObject();
    const QString name = tool.value( QStringLiteral( "name" ) ).toString();
    const QString canonicalId = tool.value( QStringLiteral( "canonicalId" ) ).toString();
    if ( !value.isObject() || !wireName.match( name ).hasMatch() || canonicalId.isEmpty()
         || m_canonicalNames.contains( name ) || canonicalIds.contains( canonicalId )
         || !tool.value( QStringLiteral( "parameters" ) ).isObject()
         || tool.value( QStringLiteral( "parameters" ) ).toObject().value( QStringLiteral( "type" ) ).toString() != QStringLiteral( "object" ) )
    {
      valid = false;
      break;
    }
    m_canonicalNames.insert( name, canonicalId );
    canonicalIds.insert( canonicalId );
    m_wireTools.append( QJsonObject{
      { QStringLiteral( "type" ), QStringLiteral( "function" ) },
      { QStringLiteral( "function" ), QJsonObject{ { QStringLiteral( "name" ), name },
                                                  { QStringLiteral( "description" ), tool.value( QStringLiteral( "description" ) ).toString() },
                                                  { QStringLiteral( "parameters" ), tool.value( QStringLiteral( "parameters" ) ) } } } } );
  }
  if ( !valid )
  {
    m_canonicalNames.clear();
    m_wireTools = {};
    m_config.profile.apiKey.clear();
    emit statusChanged( {}, QStringLiteral( "invalid_tool_catalog" ) );
    emit settled( {}, false, tr( "Invalid native conversation catalog or session." ) );
    return;
  }
  m_client->setProfile( config.profile );
  m_ready = true;
  emit ready();
}

bool NativeConversationRuntime::submitPrompt( const QString &runId, const QString &text )
{
  if ( !m_ready || m_active || runId.isEmpty() || m_usedRuns.contains( runId ) || text.toUtf8().size() > 64 * 1024 ) return false;
  // The diagnostic run opt-in controls the wire option for this whole run.
  // Existing endpoints retain their old request body while tracing is off.
  m_client->setIncludeUsage( sicnu::runtime::observability::trace::Trace::enabled() );
  ++m_epoch;
  m_active = true;
  m_runId = runId;
  m_usedRuns.insert( runId );
  m_seenCalls.clear();
  m_modelRequests = 0;
  m_toolAttempts = 0;
  m_batch.clear();
  m_batchPublished = false;
  m_history = QJsonArray{
    QJsonObject{ { QStringLiteral( "role" ), QStringLiteral( "system" ) }, { QStringLiteral( "content" ), m_config.systemPrompt } },
    QJsonObject{ { QStringLiteral( "role" ), QStringLiteral( "user" ) }, { QStringLiteral( "content" ), text } } };
  const quint64 epoch = m_epoch;
  // Also queue a new prompt: a settled signal consumer may submit it while
  // the old transport is still unwinding its final callback.
  QTimer::singleShot( 0, this, [this, epoch] {
    if ( m_active && m_epoch == epoch ) sendNextResponse();
  } );
  emit runStarted( runId );
  return true;
}

void NativeConversationRuntime::sendNextResponse()
{
  if ( !m_active || m_transportActive ) return;
  if ( m_modelRequests >= kMaxModelRequests )
  {
    finishRun( false, tr( "Native model request budget exhausted." ) );
    return;
  }
  ++m_modelRequests;
  m_content.clear();
  m_reasoning.clear();
  m_responseError.clear();
  m_batch.clear();
  m_batchPublished = false;
  m_transportActive = true;
  const quint64 epoch = m_epoch;
  QPointer<NativeConversationRuntime> alive( this );
  const QString run = m_runId;
  emit responseStarted( run );
  if ( !alive || !m_active || m_epoch != epoch ) return;
  emit statusChanged( run, QStringLiteral( "model_running" ) );
  if ( !alive || !m_active || m_epoch != epoch ) return;
  emit modelRequestStarted( run );
  if ( !alive || !m_active || m_epoch != epoch ) return;
  m_client->sendChatCompletion( m_history, m_wireTools );
}

void NativeConversationRuntime::collectToolCall( const QJsonObject &call )
{
  if ( !m_active || !m_transportActive ) return;
  if ( m_toolAttempts >= kMaxToolAttempts )
  {
    m_responseError = tr( "Native tool attempt budget exhausted." );
    emit toolCallRefused( m_runId, { { QStringLiteral( "reason" ), QStringLiteral( "tool_budget_exhausted" ) } } );
    return;
  }
  ++m_toolAttempts;
  if ( m_batch.size() >= kMaxPendingTools )
  {
    // Collection precedes dispatch: exceeding the pending bound invalidates
    // the whole response, never a partially executed prefix of the batch.
    m_responseError = tr( "Native pending tool batch limit exceeded." );
    emit toolCallRefused( m_runId, { { QStringLiteral( "reason" ), QStringLiteral( "pending_tool_limit" ) } } );
    return;
  }
  const QString id = call.value( QStringLiteral( "id" ) ).toString();
  const QJsonObject function = call.value( QStringLiteral( "function" ) ).toObject();
  if ( id.isEmpty() || id.toUtf8().size() > 256 || m_seenCalls.contains( id )
       || !function.value( QStringLiteral( "arguments" ) ).isObject() )
  {
    m_responseError = tr( "Native model tool identity or arguments were refused." );
    emit toolCallRefused( m_runId, { { QStringLiteral( "reason" ), QStringLiteral( "invalid_or_duplicate_tool_call" ) } } );
    return;
  }
  m_seenCalls.insert( id );
  PendingCall pending;
  pending.wireCall = call;
  QJsonObject wireFunction = function;
  wireFunction[QStringLiteral( "arguments" )] = QString::fromUtf8(
    QJsonDocument( function.value( QStringLiteral( "arguments" ) ).toObject() ).toJson( QJsonDocument::Compact ) );
  pending.wireCall[QStringLiteral( "function" )] = wireFunction;
  const QString name = function.value( QStringLiteral( "name" ) ).toString();
  const auto mapping = m_canonicalNames.constFind( name );
  if ( mapping == m_canonicalNames.cend() )
  {
    pending.complete = true;
    pending.result = toolError( QStringLiteral( "unknown_tool" ) );
    // Preserve the wire call/result for model correction, but never infer a
    // canonical ID from underscores or dispatch an unexported tool.
    m_batch.append( pending );
    emit toolCallRefused( m_runId, { { QStringLiteral( "reason" ), QStringLiteral( "unknown_tool" ) } } );
    return;
  }
  pending.hostCall = call;
  QJsonObject hostFunction = function;
  hostFunction[QStringLiteral( "name" )] = mapping.value();
  pending.hostCall[QStringLiteral( "function" )] = hostFunction;
  m_batch.append( pending );
}

void NativeConversationRuntime::collectRefusal( const QJsonObject &detail )
{
  if ( !m_active || !m_transportActive ) return;
  ++m_toolAttempts;
  m_responseError = m_toolAttempts > kMaxToolAttempts
    ? tr( "Native tool attempt budget exhausted." )
    : tr( "Native model returned a malformed tool call." );
  // Only the transport's controlled reason code is a diagnostic. Do not
  // forward raw model names/argument text that might echo a secret.
  emit toolCallRefused( m_runId, { { QStringLiteral( "reason" ), detail.value( QStringLiteral( "reason" ) ).toString() } } );
}

QJsonObject NativeConversationRuntime::assistantMessage() const
{
  QJsonObject message{ { QStringLiteral( "role" ), QStringLiteral( "assistant" ) },
                       { QStringLiteral( "content" ), m_content } };
  if ( !m_reasoning.isEmpty() ) message[QStringLiteral( "reasoning_content" )] = m_reasoning;
  return message;
}

void NativeConversationRuntime::finishResponse()
{
  if ( !m_responseError.isEmpty() )
  {
    finishRun( false, m_responseError );
    return;
  }
  if ( m_batch.isEmpty() )
  {
    m_history.append( assistantMessage() );
    m_history = pruneHistory( m_history );
    if ( m_client->finishReason() == QStringLiteral( "length" ) )
      finishRun( false, tr( "Native model response reached its output limit." ) );
    else
      finishRun( false );
    return;
  }
  QJsonObject assistant = assistantMessage();
  QJsonArray calls;
  for ( const auto &pending : m_batch ) calls.append( pending.wireCall );
  assistant[QStringLiteral( "tool_calls" )] = calls;
  m_history.append( assistant );
  m_batchPublished = true;
  m_dispatchingBatch = true;
  const quint64 epoch = m_epoch;
  const QString run = m_runId;
  const QString revision = m_config.toolRevision;
  QPointer<NativeConversationRuntime> alive( this );
  emit statusChanged( run, QStringLiteral( "awaiting_tools" ) );
  if ( !alive || !m_active || m_epoch != epoch ) return;
  for ( qsizetype i = 0; i < m_batch.size(); ++i )
  {
    if ( m_batch[i].complete ) continue;
    m_batch[i].requested = true;
    // A direct consumer may cancel/clear the batch while other connected
    // consumers are still reading these signal arguments.
    const QJsonObject call = m_batch[i].hostCall;
    emit toolCallRequested( run, call, revision );
    if ( !alive || !m_active || m_epoch != epoch ) return;
  }
  m_dispatchingBatch = false;
  scheduleAdvance();
}

bool NativeConversationRuntime::completeTool( const QString &runId, const QString &callId, const QJsonObject &result )
{
  if ( !m_active || runId != m_runId ) return false;
  for ( auto &pending : m_batch )
  {
    if ( pending.wireCall.value( QStringLiteral( "id" ) ).toString() != callId ) continue;
    if ( !pending.requested || pending.complete ) return false;
    pending.complete = true;
    pending.result = QJsonDocument( result ).toJson( QJsonDocument::Compact ).size() <= kMaxToolResultBytes
      ? result : toolError( QStringLiteral( "tool_result_too_large" ) );
    scheduleAdvance();
    return true;
  }
  return false;
}

void NativeConversationRuntime::scheduleAdvance()
{
  if ( m_advanceScheduled || !m_active || m_transportActive || !m_batchPublished || m_dispatchingBatch ) return;
  for ( const auto &pending : m_batch ) if ( !pending.complete ) return;
  m_advanceScheduled = true;
  const quint64 epoch = m_epoch;
  QTimer::singleShot( 0, this, [this, epoch] {
    // A stale callback must not reset a new run's scheduling flag.
    if ( !m_active || m_epoch != epoch ) return;
    m_advanceScheduled = false;
    if ( m_transportActive || !m_batchPublished ) return;
    for ( const auto &pending : m_batch )
    {
      if ( !pending.complete ) return;
      m_history.append( QJsonObject{
        { QStringLiteral( "role" ), QStringLiteral( "tool" ) },
        { QStringLiteral( "tool_call_id" ), pending.wireCall.value( QStringLiteral( "id" ) ) },
        { QStringLiteral( "content" ), QString::fromUtf8( QJsonDocument( pending.result ).toJson( QJsonDocument::Compact ) ) } } );
    }
    m_history.append( QJsonObject{
      { QStringLiteral( "role" ), QStringLiteral( "user" ) },
      { QStringLiteral( "content" ), QStringLiteral(
        "Continue the user's original task using the tool results. Use further tools when needed; "
        "produce the final answer only once the task is complete. Report tool or verification "
        "failures explicitly and never claim success for a failed operation." ) } } );
    m_history = pruneHistory( m_history );
    m_batchPublished = false;
    sendNextResponse();
  } );
}

void NativeConversationRuntime::finishRun( bool aborted, const QString &error )
{
  if ( !m_active ) return;
  const QString run = m_runId;
  // Keep an owning copy: cancellation and direct signals may reenter and replace the caller's error storage.
  const QString safeError = error; // NOLINT(performance-unnecessary-copy-initialization)
  ++m_epoch;
  m_active = false;
  m_transportActive = false;
  m_advanceScheduled = false;
  m_batchPublished = false;
  m_dispatchingBatch = false;
  m_batch.clear();
  m_runId.clear();
  m_client->cancel(); // Silent transport cancellation needs our explicit terminal.
  QPointer<NativeConversationRuntime> alive( this );
  if ( aborted || !safeError.isEmpty() )
  {
    emit cancelOwnedToolsRequested( run );
    if ( !alive ) return;
  }
  emit statusChanged( run, aborted ? QStringLiteral( "cancelled" )
                                 : safeError.isEmpty() ? QStringLiteral( "completed" ) : QStringLiteral( "failed" ) );
  if ( alive ) emit settled( run, aborted, safeError );
}

void NativeConversationRuntime::cancelRun()
{
  finishRun( true );
}

void NativeConversationRuntime::closeSession()
{
  m_ready = false;
  QPointer<NativeConversationRuntime> alive( this );
  cancelRun();
  if ( !alive ) return;
  ++m_epoch;
  m_client->cancel();
  m_client->setProfile( {} );
  m_config = {};
  m_canonicalNames.clear();
  m_wireTools = {};
  m_history = {};
  m_seenCalls.clear();
  m_usedRuns.clear();
}

QJsonArray NativeConversationRuntime::pruneHistory( const QJsonArray &history )
{
  if ( history.size() <= kMaxHistoryMessages ) return history;
  int start = static_cast<int>( history.size() ) - kMaxHistoryMessages;
  while ( start < history.size()
          && history.at( start ).toObject().value( QStringLiteral( "role" ) ).toString() == QStringLiteral( "tool" ) )
    ++start;
  QJsonArray pruned;
  for ( int i = 0; i < history.size(); ++i ) if ( i == 0 || i >= start ) pruned.append( history.at( i ) );
  return pruned;
}

} // namespace sicnu::agent::runtime

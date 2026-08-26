// src/agent/llm_streaming_client.cpp
#include "llm_streaming_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslError>

namespace sicnu::agent
{

LlmStreamingClient::LlmStreamingClient( QObject *parent )
  : QObject( parent )
{
  m_networkManager = new QNetworkAccessManager( this );
  m_profile = LlmConfigManager::activeProfile();
}

LlmStreamingClient::~LlmStreamingClient()
{
  cancel();
}

void LlmStreamingClient::setProfile( const LlmProviderProfile &profile )
{
  m_profile = profile;
}

LlmProviderProfile LlmStreamingClient::profile() const
{
  return m_profile;
}

ChatRequestPayload LlmStreamingClient::buildChatRequest( const LlmProviderProfile &profile,
                                                         const QJsonArray &messages,
                                                         const QJsonArray &tools )
{
  ChatRequestPayload payload;

  QString endpointUrl = profile.baseUrl;
  if ( !endpointUrl.endsWith( QStringLiteral( "/chat/completions" ) ) )
  {
    if ( !endpointUrl.endsWith( QStringLiteral( "/" ) ) )
      endpointUrl += QStringLiteral( "/" );
    endpointUrl += QStringLiteral( "chat/completions" );
  }

  payload.request.setUrl( QUrl( endpointUrl ) );
  payload.request.setTransferTimeout( 120000 );
  payload.request.setHeader( QNetworkRequest::ContentTypeHeader, QStringLiteral( "application/json" ) );

  if ( !profile.apiKey.isEmpty() )
  {
    payload.request.setRawHeader( "Authorization", QString( "Bearer %1" ).arg( profile.apiKey ).toUtf8() );
  }

  QJsonObject requestObj;
  requestObj[QStringLiteral( "model" )] = profile.modelName;
  requestObj[QStringLiteral( "messages" )] = messages;
  // The transport is SSE-only: the parser understands `data:` lines and
  // nothing else, so the request always asks for a stream. profile.stream is
  // kept for persistence/UI but no longer shapes the wire format (ADR 0049).
  requestObj[QStringLiteral( "stream" )] = true;
  requestObj[QStringLiteral( "temperature" )] = profile.temperature;

  if ( !tools.isEmpty() )
  {
    requestObj[QStringLiteral( "tools" )] = tools;
  }

  payload.body = QJsonDocument( requestObj ).toJson( QJsonDocument::Compact );
  return payload;
}

void LlmStreamingClient::sendChatCompletion( const QJsonArray &messages, const QJsonArray &tools )
{
  cancel();
  m_buffer.clear();
  m_toolCalls.clear();
  m_finishedEmitted = false;
  m_lastFinishReason.clear();

  const ChatRequestPayload payload = buildChatRequest( m_profile, messages, tools );

  m_currentReply = m_networkManager->post( payload.request, payload.body );

  if ( m_currentReply )
  {
    connect( m_currentReply, &QNetworkReply::readyRead, this, &LlmStreamingClient::onReadyRead );
    connect( m_currentReply, &QNetworkReply::finished, this, &LlmStreamingClient::onReplyFinished );
    connect( m_currentReply, &QNetworkReply::errorOccurred, this, &LlmStreamingClient::onReplyError );
    connect( m_currentReply, &QNetworkReply::sslErrors, this, [this]( const QList<QSslError> &errors ) {
      QStringList msgs;
      for ( const auto &e : errors )
        msgs.append( e.errorString() );
      emit errorOccurred( QStringLiteral( "SSL Error: %1" ).arg( msgs.join( QStringLiteral( "; " ) ) ) );
    } );
  }
}

void LlmStreamingClient::cancel()
{
  if ( m_currentReply )
  {
    QPointer<QNetworkReply> reply = m_currentReply;
    m_currentReply = nullptr;
    reply->abort();
    if ( reply )
    {
      reply->deleteLater();
    }
  }
}

void LlmStreamingClient::onReadyRead()
{
  if ( !m_currentReply )
    return;

  m_buffer.append( m_currentReply->readAll() );

  while ( true )
  {
    int newlinePos = m_buffer.indexOf( '\n' );
    if ( newlinePos == -1 )
      break;

    QByteArray rawLine = m_buffer.left( newlinePos );
    m_buffer.remove( 0, newlinePos + 1 );

    QString line = QString::fromUtf8( rawLine ).trimmed();
    if ( line.isEmpty() )
      continue;

    parseSseLine( line );
  }
}

void LlmStreamingClient::parseSseLine( const QString &line )
{
  if ( !line.startsWith( QStringLiteral( "data:" ) ) )
    return;

  QString jsonStr = line.mid( 5 ).trimmed();
  if ( jsonStr == QStringLiteral( "[DONE]" ) )
  {
    // A [DONE] token finalizes any accumulated tool call. emitParsedToolCallOnce
    // clears the accumulation, so the onReplyFinished fallback stays silent.
    emitParsedToolCallOnce();

    if ( !m_finishedEmitted )
    {
      m_finishedEmitted = true;
      emit finished();
    }
    return;
  }

  QJsonDocument doc = QJsonDocument::fromJson( jsonStr.toUtf8() );
  if ( !doc.isObject() )
    return;

  QJsonObject obj = doc.object();
  if ( !obj.contains( QStringLiteral( "choices" ) ) )
    return;

  QJsonArray choices = obj[QStringLiteral( "choices" )].toArray();
  if ( choices.isEmpty() )
    return;

  QJsonObject choice0 = choices[0].toObject();
  if (choice0.contains(QStringLiteral("finish_reason")) && choice0[QStringLiteral("finish_reason")].isString())
  {
    m_lastFinishReason = choice0[QStringLiteral("finish_reason")].toString();
  }
  QJsonObject delta = choice0[QStringLiteral( "delta" )].toObject();

  // 1. DeepSeek-R1 reasoning content (<think>)
  if ( delta.contains( QStringLiteral( "reasoning_content" ) ) && delta[QStringLiteral( "reasoning_content" )].isString() )
  {
    QString reasoning = delta[QStringLiteral( "reasoning_content" )].toString();
    if ( !reasoning.isEmpty() )
      emit reasoningTokenReceived( reasoning );
  }

  // 2. Regular content text
  if ( delta.contains( QStringLiteral( "content" ) ) && delta[QStringLiteral( "content" )].isString() )
  {
    QString content = delta[QStringLiteral( "content" )].toString();
    if ( !content.isEmpty() )
      emit contentTokenReceived( content );
  }

  // 3. Tool call function streaming
  if ( delta.contains( QStringLiteral( "tool_calls" ) ) && delta[QStringLiteral( "tool_calls" )].isArray() )
  {
    QJsonArray toolCalls = delta[QStringLiteral( "tool_calls" )].toArray();
    for ( const auto &tcVal : toolCalls )
    {
      QJsonObject tcObj = tcVal.toObject();
      int index = tcObj.contains( QStringLiteral( "index" ) ) ? tcObj[QStringLiteral( "index" )].toInt( 0 ) : 0;
      auto &accu = m_toolCalls[index];

      if ( tcObj.contains( QStringLiteral( "id" ) ) && !tcObj[QStringLiteral( "id" )].toString().isEmpty() )
      {
        accu.id = tcObj[QStringLiteral( "id" )].toString();
      }

      if ( tcObj.contains( QStringLiteral( "function" ) ) )
      {
        QJsonObject funcObj = tcObj[QStringLiteral( "function" )].toObject();
        if ( funcObj.contains( QStringLiteral( "name" ) ) && !funcObj[QStringLiteral( "name" )].toString().isEmpty() )
        {
          accu.name = funcObj[QStringLiteral( "name" )].toString();
        }
        if ( funcObj.contains( QStringLiteral( "arguments" ) ) && funcObj[QStringLiteral( "arguments" )].isString() )
        {
          accu.arguments += funcObj[QStringLiteral( "arguments" )].toString();
        }
      }
    }
  }
}

void LlmStreamingClient::emitParsedToolCallOnce()
{
  if ( m_toolCalls.empty() )
    return;

  for ( const auto &pair : m_toolCalls )
  {
    const ToolCallAccumulator &accu = pair.second;
    if ( accu.name.isEmpty() )
      continue;

    QJsonObject funcObj;
    funcObj[QStringLiteral( "name" )] = accu.name;

    QJsonDocument argsDoc = QJsonDocument::fromJson( accu.arguments.toUtf8() );
    if ( argsDoc.isObject() )
    {
      funcObj[QStringLiteral( "arguments" )] = argsDoc.object();
    }
    else
    {
      QString rawArgs = accu.arguments.trimmed();
      if ( rawArgs.startsWith( '"' ) && rawArgs.endsWith( '"' ) && rawArgs.size() > 2 )
      {
        const QJsonDocument arrDoc = QJsonDocument::fromJson( QString( "[%1]" ).arg( rawArgs ).toUtf8() );
        if ( arrDoc.isArray() && !arrDoc.array().isEmpty() && arrDoc.array()[0].isString() )
        {
          const QString unescapedStr = arrDoc.array()[0].toString();
          const QJsonDocument nestedDoc = QJsonDocument::fromJson( unescapedStr.toUtf8() );
          if ( nestedDoc.isObject() )
          {
            funcObj[QStringLiteral( "arguments" )] = nestedDoc.object();
          }
          else
          {
            funcObj[QStringLiteral( "arguments" )] = accu.arguments;
          }
        }
        else
        {
          funcObj[QStringLiteral( "arguments" )] = accu.arguments;
        }
      }
      else
      {
        funcObj[QStringLiteral( "arguments" )] = accu.arguments;
      }
    }

    QJsonObject toolCallObj;
    toolCallObj[QStringLiteral( "id" )] = accu.id;
    toolCallObj[QStringLiteral( "type" )] = QStringLiteral( "function" );
    toolCallObj[QStringLiteral( "function" )] = funcObj;

    emit toolCallParsed( toolCallObj );
  }

  m_toolCalls.clear();
}

void LlmStreamingClient::onReplyFinished()
{
  if (!m_currentReply)
    return;
  QNetworkReply *reply = m_currentReply;
  m_currentReply = nullptr;
  reply->deleteLater();

  if ( !m_buffer.isEmpty() )
  {
    QString line = QString::fromUtf8( m_buffer ).trimmed();
    if ( !line.isEmpty() )
      parseSseLine( line );
    m_buffer.clear();
  }

  // If no [DONE] token finalized the tool call, emit it now. No-op when the
  // [DONE] path already emitted it — a parsed tool call is emitted exactly once.
  emitParsedToolCallOnce();

  if (reply->error() != QNetworkReply::NoError && reply->error() != QNetworkReply::OperationCanceledError)
  {
    if (!m_finishedEmitted)
    {
      emit errorOccurred(reply->errorString());
    }
  }
  if ( !m_finishedEmitted )
  {
    m_finishedEmitted = true;
    emit finished();
  }
}

void LlmStreamingClient::onReplyError( QNetworkReply::NetworkError code )
{
  if ( code != QNetworkReply::OperationCanceledError && m_currentReply )
  {
    emit errorOccurred( m_currentReply->errorString() );
    if (!m_finishedEmitted)
    {
      m_finishedEmitted = true;
      emit finished();
    }
  }
}

} // namespace sicnu::agent

// src/agent/llm_streaming_client.cpp
#include "llm_streaming_client.h"
#include "processing/framework/agent_tool_call_exporter.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <json/json.h>

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

void LlmStreamingClient::sendChatCompletion( const QJsonArray &messages, bool enableTools )
{
  cancel();
  m_buffer.clear();
  m_toolCallId.clear();
  m_toolFunctionName.clear();
  m_toolArgumentsBuffer.clear();

  QString endpointUrl = m_profile.baseUrl;
  if ( !endpointUrl.endsWith( QStringLiteral( "/chat/completions" ) ) )
  {
    if ( !endpointUrl.endsWith( QStringLiteral( "/" ) ) )
      endpointUrl += QStringLiteral( "/" );
    endpointUrl += QStringLiteral( "chat/completions" );
  }

  QNetworkRequest request;
  request.setUrl( QUrl( endpointUrl ) );
  request.setTransferTimeout( 10000 );
  request.setHeader( QNetworkRequest::ContentTypeHeader, QStringLiteral( "application/json" ) );

  if ( !m_profile.apiKey.isEmpty() )
  {
    request.setRawHeader( "Authorization", QString( "Bearer %1" ).arg( m_profile.apiKey ).toUtf8() );
  }

  QJsonObject requestObj;
  requestObj[QStringLiteral( "model" )] = m_profile.modelName;
  requestObj[QStringLiteral( "messages" )] = messages;
  requestObj[QStringLiteral( "stream" )] = m_profile.stream;
  requestObj[QStringLiteral( "temperature" )] = m_profile.temperature;

  if ( enableTools )
  {
    Json::Value cppTools = processing::AtomicAlgorithmRegistry::instance().exportOpenAiToolDefinitions();
    Json::StreamWriterBuilder writer;
    std::string toolsJsonStr = Json::writeString( writer, cppTools );

    QJsonDocument toolsDoc = QJsonDocument::fromJson( QByteArray::fromStdString( toolsJsonStr ) );
    if ( toolsDoc.isArray() && !toolsDoc.array().isEmpty() )
    {
      requestObj[QStringLiteral( "tools" )] = toolsDoc.array();
    }
  }

  QJsonDocument doc( requestObj );
  QByteArray requestData = doc.toJson( QJsonDocument::Compact );

  m_currentReply = m_networkManager->post( request, requestData );

  if ( m_currentReply )
  {
    connect( m_currentReply, &QNetworkReply::readyRead, this, &LlmStreamingClient::onReadyRead );
    connect( m_currentReply, &QNetworkReply::finished, this, &LlmStreamingClient::onReplyFinished );
    connect( m_currentReply, &QNetworkReply::errorOccurred, this, &LlmStreamingClient::onReplyError );
  }
}

void LlmStreamingClient::cancel()
{
  if ( m_currentReply )
  {
    m_currentReply->abort();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
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

    emit finished();
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
      if ( tcObj.contains( QStringLiteral( "id" ) ) && !tcObj[QStringLiteral( "id" )].toString().isEmpty() )
      {
        m_toolCallId = tcObj[QStringLiteral( "id" )].toString();
      }

      if ( tcObj.contains( QStringLiteral( "function" ) ) )
      {
        QJsonObject funcObj = tcObj[QStringLiteral( "function" )].toObject();
        if ( funcObj.contains( QStringLiteral( "name" ) ) && !funcObj[QStringLiteral( "name" )].toString().isEmpty() )
        {
          m_toolFunctionName = funcObj[QStringLiteral( "name" )].toString();
        }
        if ( funcObj.contains( QStringLiteral( "arguments" ) ) && funcObj[QStringLiteral( "arguments" )].isString() )
        {
          m_toolArgumentsBuffer += funcObj[QStringLiteral( "arguments" )].toString();
        }
      }
    }
  }
}

void LlmStreamingClient::emitParsedToolCallOnce()
{
  if ( m_toolFunctionName.isEmpty() )
    return;

  QJsonObject funcObj;
  funcObj[QStringLiteral( "name" )] = m_toolFunctionName;

  QJsonDocument argsDoc = QJsonDocument::fromJson( m_toolArgumentsBuffer.toUtf8() );
  if ( argsDoc.isObject() )
  {
    funcObj[QStringLiteral( "arguments" )] = argsDoc.object();
  }
  else
  {
    funcObj[QStringLiteral( "arguments" )] = m_toolArgumentsBuffer;
  }

  QJsonObject toolCallObj;
  toolCallObj[QStringLiteral( "id" )] = m_toolCallId;
  toolCallObj[QStringLiteral( "type" )] = QStringLiteral( "function" );
  toolCallObj[QStringLiteral( "function" )] = funcObj;

  emit toolCallParsed( toolCallObj );

  m_toolFunctionName.clear();
  m_toolArgumentsBuffer.clear();
  m_toolCallId.clear();
}

void LlmStreamingClient::onReplyFinished()
{
  if ( m_currentReply && m_currentReply->error() == QNetworkReply::NoError )
  {
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

    emit finished();
  }
}

void LlmStreamingClient::onReplyError( QNetworkReply::NetworkError code )
{
  if ( code != QNetworkReply::OperationCanceledError && m_currentReply )
  {
    emit errorOccurred( m_currentReply->errorString() );
  }
}

} // namespace sicnu::agent

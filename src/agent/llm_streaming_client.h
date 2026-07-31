// src/agent/llm_streaming_client.h
#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>

#include "llm_config_manager.h"

namespace sicnu::agent
{

class LlmStreamingClient : public QObject
{
  Q_OBJECT

  public:
    explicit LlmStreamingClient( QObject *parent = nullptr );
    ~LlmStreamingClient() override;

    void setProfile( const LlmProviderProfile &profile );
    LlmProviderProfile profile() const;

    /**
     * Sends a chat completion request to the configured LLM API.
     * Auto-injects registered algorithm tools from AtomicAlgorithmRegistry if enableTools is true.
     */
    void sendChatCompletion( const QJsonArray &messages, bool enableTools = true );

    /**
     * Cancels any in-flight streaming HTTP request.
     */
    void cancel();

    /**
     * Utility: Parse a mock SSE line stream for testing purposes without network requests.
     */
    void parseSseLine( const QString &line );

  signals:
    void reasoningTokenReceived( const QString &reasoningText );
    void contentTokenReceived( const QString &textDelta );
    // Emitted exactly once per parsed tool-call envelope (OpenAI shape:
    // {id, type, function:{name, arguments}}). The client never executes tool
    // calls — it is pure transport; execution is the caller's decision.
    void toolCallParsed( const QJsonObject &toolCallJson );
    void finished();
    void errorOccurred( const QString &errorMessage );

  private slots:
    void onReadyRead();
    void onReplyFinished();
    void onReplyError( QNetworkReply::NetworkError code );

  private:
    /// Emits toolCallParsed for the accumulated tool call, then clears the
    /// accumulation. No-op when no tool call is pending, so a stream that
    /// ended with [DONE] emits exactly once (the [DONE] path and the
    /// reply-finished path never double-emit).
    void emitParsedToolCallOnce();

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    LlmProviderProfile m_profile;
    QByteArray m_buffer;

    QString m_toolCallId;
    QString m_toolFunctionName;
    QString m_toolArgumentsBuffer;
};

} // namespace sicnu::agent

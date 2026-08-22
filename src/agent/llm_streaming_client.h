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
#include "sicnu_agent_export.h"

namespace sicnu::agent
{

/// Wire payload assembled by LlmStreamingClient::buildChatRequest — the
/// QNetworkRequest plus its JSON body. Exposed so the request builder is a
/// pure, network-free test seam.
struct ChatRequestPayload
{
  QNetworkRequest request;
  QByteArray body;
};

class SICNU_AGENT_EXPORT LlmStreamingClient : public QObject
{
  Q_OBJECT

  public:
    explicit LlmStreamingClient( QObject *parent = nullptr );
    ~LlmStreamingClient() override;

    void setProfile( const LlmProviderProfile &profile );
    LlmProviderProfile profile() const;

    /// Pure request builder: normalizes the endpoint, sets headers (Bearer
    /// auth when the profile has an apiKey), and assembles the JSON body —
    /// no network access, unit-testable. The body always asks for a stream:
    /// the transport is SSE-only, so profile.stream no longer shapes the wire.
    static ChatRequestPayload buildChatRequest( const LlmProviderProfile &profile,
                                                const QJsonArray &messages,
                                                const QJsonArray &tools = QJsonArray() );

    /**
     * Sends a chat completion request to the configured LLM API.
     * Tools are caller-supplied: an empty tools array sends none.
     */
    void sendChatCompletion( const QJsonArray &messages, const QJsonArray &tools = QJsonArray() );

    /**
     * Cancels any in-flight streaming HTTP request.
     */
    void cancel();

    /**
     * Utility: Parse a mock SSE line stream for testing purposes without network requests.
     */
    void parseSseLine( const QString &line );

    QString finishReason() const { return m_lastFinishReason; }

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

    struct ToolCallAccumulator
    {
      QString id;
      QString name;
      QString arguments;
    };

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    LlmProviderProfile m_profile;
    QByteArray m_buffer;

    std::map<int, ToolCallAccumulator> m_toolCalls;
    bool m_finishedEmitted = false;
    QString m_lastFinishReason;
};

} // namespace sicnu::agent

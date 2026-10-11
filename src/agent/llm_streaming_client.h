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

    /// Bounded accumulation: a tool call whose streamed arguments exceed this
    /// many characters is refused as oversized (never accumulated unbounded).
    static constexpr int kMaxToolCallArgumentChars = 1 << 20; // 1 MiB
    /// Bounded SSE line: a data: line longer than this is refused as
    /// oversized — a hostile stream cannot make the client buffer without
    /// limit, on the network path and on the test seam alike.
    static constexpr int kMaxSseLineChars = 8 << 20; // 8 MiB
    /// Bounded in-flight tool calls: distinct streamed indices beyond this
    /// are refused (a hostile stream can mint unlimited indices).
    static constexpr int kMaxToolCallIndices = 64;

  signals:
    void reasoningTokenReceived( const QString &reasoningText );
    void contentTokenReceived( const QString &textDelta );
    // Emitted exactly once per parsed tool-call envelope (OpenAI shape:
    // {id, type, function:{name, arguments}}). The client never executes tool
    // calls — it is pure transport; execution is the caller's decision.
    void toolCallParsed( const QJsonObject &toolCallJson );
    /// Emitted when a streamed tool call is REFUSED before toolCallParsed:
    /// truncated arguments (finish_reason=length), unparseable argument JSON,
    /// a missing function name, or an oversized line/accumulation. Payload:
    /// {reason, name?, finish_reason?, size?}. #701 refused these calls but
    /// only logged the refusal — the session and journal cannot account for
    /// a drop they cannot observe.
    void malformedToolCall( const QJsonObject &detail );
    void finished();
    void errorOccurred( const QString &errorMessage );

  private slots:
    void onReadyRead();
    void onReplyFinished();
    void onReplyError( QNetworkReply::NetworkError code );

  private:
    /// Clears the accumulated tool calls, then emits one toolCallParsed per
    /// valid call from the detached snapshot. Detaching first matters because
    /// a direct-connected consumer may re-enter sendChatCompletion from the
    /// signal, which would otherwise clear() the map mid-iteration (#1452).
    /// No-op when no tool call is pending, so a stream that ended with [DONE]
    /// emits exactly once (the [DONE] path and the reply-finished path never
    /// double-emit).
    void emitParsedToolCallOnce();

    /// Declares the current stream failed and ends the turn: detaches the
    /// reply (cancel()), then emits errorOccurred followed by finished —
    /// unless a terminal pair already went out. cancel() on its own is
    /// SILENT: abort() surfaces as OperationCanceledError (skipped by
    /// onReplyError) and the reply's finished() is dropped by
    /// onReplyFinished's null-reply guard, so a refusal that only cancels
    /// leaves the caller waiting on a turn that will never signal again.
    void failStream( const QString &errorMessage );

    struct ToolCallAccumulator
    {
      QString id;
      QString name;
      QString arguments;
      bool oversized = false; ///< argument stream crossed the bound; accumulation stops
    };

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    LlmProviderProfile m_profile;
    QByteArray m_buffer;

    std::map<int, ToolCallAccumulator> m_toolCalls;
    bool m_finishedEmitted = false;
    /// Monotonic stream generation, bumped by every sendChatCompletion. The
    /// terminal-state guards compare against the generation observed before
    /// tool calls were emitted, so a re-entrant sendChatCompletion (the next
    /// turn started from a direct-connected toolCallParsed slot) — which
    /// resets m_finishedEmitted for the NEW stream — cannot make the
    /// superseded stream emit a stray finished() (#1452).
    int m_streamGeneration = 0;
    QString m_lastFinishReason;
};

} // namespace sicnu::agent

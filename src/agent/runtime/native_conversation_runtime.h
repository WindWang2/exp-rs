#pragma once

#include "agent_conversation_runtime.h"

#include <QHash>
#include <QSet>
#include <QVector>

namespace sicnu::agent
{
class LlmStreamingClient;
}

namespace sicnu::agent::runtime
{

/// Native model/tool conversation loop. Native execution and approval remain
/// entirely in AgentToolHost, shared with the Pi implementation.
class SICNU_AGENT_EXPORT NativeConversationRuntime final : public AgentConversationRuntime
{
    Q_OBJECT
  public:
    explicit NativeConversationRuntime( QObject *parent = nullptr );
    ~NativeConversationRuntime() override;

    void startSession( const ConversationConfig &config ) override;
    bool submitPrompt( const QString &runId, const QString &text ) override;
    bool completeTool( const QString &runId, const QString &callId, const QJsonObject &result ) override;
    void cancelRun() override;
    void closeSession() override;
    bool isReady() const override { return m_ready && !m_active; }

    QJsonArray messageHistory() const { return m_history; }
    static QJsonArray pruneHistory( const QJsonArray &history );
    /// Leading system prompt plus at most this many recent messages.
    static constexpr int kMaxHistoryMessages = 60;

  private:
    struct PendingCall
    {
      QJsonObject wireCall;
      QJsonObject hostCall;
      QJsonObject result;
      bool requested = false;
      bool complete = false;
    };

    void collectToolCall( const QJsonObject &call );
    void collectRefusal( const QJsonObject &detail );
    void finishResponse();
    void scheduleAdvance();
    void sendNextResponse();
    void finishRun( bool aborted, const QString &error = {} );
    QJsonObject assistantMessage() const;

    LlmStreamingClient *m_client = nullptr;
    ConversationConfig m_config;
    QHash<QString, QString> m_canonicalNames;
    QJsonArray m_wireTools;
    QJsonArray m_history;
    QVector<PendingCall> m_batch;
    QSet<QString> m_seenCalls;
    QSet<QString> m_usedRuns;
    QString m_runId;
    QString m_content;
    QString m_reasoning;
    QString m_responseError;
    quint64 m_epoch = 0;
    int m_modelRequests = 0;
    int m_toolAttempts = 0;
    bool m_ready = false;
    bool m_active = false;
    bool m_transportActive = false;
    bool m_batchPublished = false;
    bool m_dispatchingBatch = false;
    bool m_advanceScheduled = false;
};

} // namespace sicnu::agent::runtime

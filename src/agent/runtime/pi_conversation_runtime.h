#pragma once

#include "agent_conversation_runtime.h"

#include <QHash>
#include <QSet>

namespace sicnu::agent::runtime
{
class PiProcess;

/// Product conversation adapter. PiProcess owns the verified child; the Qt
/// caller remains the authority for approval, native tasks and asset commits.
class SICNU_AGENT_EXPORT PiConversationRuntime final : public AgentConversationRuntime
{
    Q_OBJECT
  public:
    explicit PiConversationRuntime( const QString &absolutePrefixPath, QObject *parent = nullptr );
    ~PiConversationRuntime() override;

    void startSession( const ConversationConfig &config ) override;
    bool submitPrompt( const QString &runId, const QString &text ) override;
    bool completeTool( const QString &runId, const QString &callId, const QJsonObject &result ) override;
    void cancelRun() override;
    void closeSession() override;
    bool isReady() const override;

  private:
    void receiveEvent( const QJsonObject &event );
    void requestHostCancellation();
    void finishRun( bool aborted, const QString &error );
    void refuseSession( const QString &code );
    bool beginResponse( const QString &runId );

    const QString m_prefixPath;
    PiProcess *m_process = nullptr;
    QHash<QString, QString> m_canonicalByProviderName;
    QSet<QString> m_pendingCalls;
    QString m_runId;
    QString m_toolRevision;
    bool m_ready = false;
    bool m_closing = false;
    bool m_cancelRequested = false;
    bool m_responseAfterUsage = false;
    bool m_sessionFailureReported = false;
};
} // namespace sicnu::agent::runtime

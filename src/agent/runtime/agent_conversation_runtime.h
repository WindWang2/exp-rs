#pragma once

#include "agent/llm_config_manager.h"
#include "agent/sicnu_agent_export.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

namespace sicnu::agent::runtime
{

struct ConversationConfig
{
  LlmProviderProfile profile;
  QString sessionId;
  QString systemPrompt;
  QString toolRevision;
  /// Host snapshot: {name, canonicalId, description, parameters}. Names are
  /// explicit mappings; implementations must never reverse a sanitizer.
  QJsonArray tools;
  QString projectId;
  QJsonObject checkpoint;
};

/// Owns conversation transport/history only. The current Qt host owns tool
/// admission, approval, native execution and committed results. A settled
/// conversation does not imply that its native work has reached terminal.
class SICNU_AGENT_EXPORT AgentConversationRuntime : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual void startSession( const ConversationConfig &config ) = 0;
    virtual bool submitPrompt( const QString &runId, const QString &text ) = 0;
    virtual bool completeTool( const QString &runId, const QString &callId,
                               const QJsonObject &result ) = 0;
    virtual void cancelRun() = 0;
    virtual void closeSession() = 0;
    virtual bool isReady() const = 0;
    /// The caller must first confirm this completed run has no owned native work.
    /// Native conversations do not provide a Pi checkpoint.
    virtual bool requestCheckpoint( const QString &, const QString & ) { return false; }

  signals:
    void ready();
    void responseStarted( const QString &runId );
    void reasoningDelta( const QString &runId, const QString &text );
    void contentDelta( const QString &runId, const QString &text );
    /// OpenAI-shaped envelope with function.name set to the canonical ID.
    void toolCallRequested( const QString &runId, const QJsonObject &call,
                            const QString &toolRevision );
    void toolCallRefused( const QString &runId, const QJsonObject &detail );
    void cancelOwnedToolsRequested( const QString &runId );
    void statusChanged( const QString &runId, const QString &code );
    void usageUpdated( const QString &runId, const QJsonObject &usage );
    void checkpointReady( const QString &runId, const QString &checkpointId, const QJsonObject &checkpoint );
    void checkpointFailed( const QString &runId, const QString &checkpointId, const QString &code );
    /// Error is a safe diagnostic, never a raw provider response/profile.
    void settled( const QString &runId, bool aborted, const QString &error );
};

} // namespace sicnu::agent::runtime

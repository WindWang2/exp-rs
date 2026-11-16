#pragma once

#include "agent/sicnu_agent_export.h"
#include "processing/framework/tool_call_dispatcher.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

namespace sicnu { class TaskCenter; }
namespace sicnu::data { class DataManager; }

namespace sicnu::agent::runtime
{

struct ToolCatalogSnapshot
{
  /// Each entry carries name, canonicalId, description and parameters.
  QJsonArray tools;
  QString revision;
  QString error;
};

/// Current-project tool admission and native execution ledger. Conversation
/// runtimes supply requests; only this host owns approvals and native IDs.
class SICNU_AGENT_EXPORT AgentToolHost : public QObject
{
    Q_OBJECT
  public:
    static constexpr int kMaxCatalogTools = 64;
    static constexpr int kMaxQueuedCalls = 16;
    static constexpr qint64 kExecutionBudgetMs = 15 * 60 * 1000;

    explicit AgentToolHost( sicnu::TaskCenter &taskCenter, QObject *parent = nullptr );
    ~AgentToolHost() override;

    /// Refuses a context change while an existing run owns native work.
    bool setContext( sicnu::data::DataManager *dataManager,
                     processing::ToolCallDispatcher::CanvasActionHandler canvasHandler = {},
                     processing::ToolCallDispatcher::InteractionActionHandler interactionHandler = {} );
    ToolCatalogSnapshot catalogSnapshot() const;
    /// Revoke approvals and publication into a replaced GUI project. Native
    /// executions remain owned until reconciled; setContext rebinds when idle.
    void invalidateProject();
    /// True when a run can be admitted at all: a DataManager bound on the
    /// host thread whose project context has not been invalidated. Lets a
    /// refused beginRun distinguish "no project" from "busy lease" (#2073).
    bool hasProjectContext() const;
    /// Call after the conversation backend is ready, immediately before its
    /// first prompt. The whole-run clock pauses only for an actual approval.
    bool beginRun( const QString &runId, const QString &toolRevision = {} );
    void requestTool( const QString &runId, const QJsonObject &envelope,
                      const QString &toolRevision );
    void resolveApproval( const QString &token, bool approved );
    void cancelRun( const QString &runId, const QString &reason = {} );
    void finishConversation( const QString &runId );

    bool isRunIdle( const QString &runId ) const;
    bool hasOwnedWork() const;
    QSet<long> taskIds( const QString &runId ) const;
    QSet<long> pipelineIds( const QString &runId ) const;
    QString callState( const QString &runId, const QString &callId ) const;

  signals:
    void toolCompleted( const QString &runId, const QString &callId, const QJsonObject &result );
    void approvalRequested( const QString &runId, const QString &token,
                            const QJsonObject &plan, const QJsonObject &envelope );
    void toolUpdated( const QString &runId, const QString &callId,
                      const QString &state, const QString &detail );
    void cancelConversationRequested( const QString &runId );
    void runIdle( const QString &runId );

  private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace sicnu::agent::runtime

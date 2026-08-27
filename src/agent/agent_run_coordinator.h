#pragma once

#include "output_verifier.h"
#include "sicnu_agent_export.h"

#include <QDateTime>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <optional>

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::agent
{

enum class AgentRunStage
{
  Understanding,
  Planning,
  Preflight,
  Queued,
  Running,
  Verifying,
  Repairing,
  Presenting,
  Completed,
  Failed,
  Canceled
};

QString agentRunStageName( AgentRunStage stage );

/// Mutable record of one agent-driven remote-sensing run.
struct SICNU_AGENT_EXPORT AgentRun
{
  QString id;
  QString request;
  AgentRunStage stage = AgentRunStage::Understanding;
  QString algorithmId;
  QVariantMap params;

  Json::Value preflightReport;
  Json::Value executionPayload;
  OutputVerification verification;

  QStringList errors;
  int repairAttempts = 0;

  QDateTime startedAtUtc;
  QDateTime completedAtUtc;
  long taskId = -1;
};

struct SICNU_AGENT_EXPORT AgentRunRequest
{
  QString algorithmId;
  QVariantMap params;
  QString userRequest;
};

/// Orchestrates a single remote-sensing request end-to-end: preflight,
/// execution, output verification, bounded repair and final presentation.
/// The coordinator never reimplements the execution spine; it drives the
/// existing ExecutionPlane / TaskCenter machinery through injected callbacks.
class SICNU_AGENT_EXPORT AgentRunCoordinator : public QObject
{
  Q_OBJECT

  public:
    /// Produces a preflight report for the proposed invocation.
    using PreflightFunction = std::function<Json::Value( const QString &algorithmId,
                                                         const QVariantMap &params )>;

    /// Submits the algorithm and returns the committed execution payload.
    /// @a outTaskId receives the TaskCenter task id; @a outCancel receives a
    /// callable that requests cancellation.
    using ExecuteFunction = std::function<Json::Value( const QString &algorithmId,
                                                       const QVariantMap &params,
                                                       long &outTaskId,
                                                       std::function<void()> &outCancel )>;

    /// Verifies the committed output path.
    using VerifyFunction = std::function<OutputVerification( const QString &committedPath,
                                                             const QString &kindHint )>;

    /// Given a failed run and the failure payload, return a repaired parameter
    /// map or std::nullopt when the failure is unrepairable.
    using RepairFunction = std::function<std::optional<QVariantMap>( const AgentRun &run,
                                                                     const Json::Value &failurePayload )>;

    /// Final map finishing hook (e.g. zoom to result).  Kept optional and
    /// injected so the coordinator has no direct GUI dependency.
    using PresentFunction = std::function<void( const AgentRun &run )>;

    explicit AgentRunCoordinator( sicnu::data::DataManager *dataManager = nullptr,
                                  QObject *parent = nullptr );

    void setPreflightFunction( PreflightFunction function );
    void setExecuteFunction( ExecuteFunction function );
    void setVerifyFunction( VerifyFunction function );
    void setRepairFunction( RepairFunction function );
    void setPresentFunction( PresentFunction function );

    /// Start a run and return its id.  The run proceeds synchronously on the
    /// calling thread; signals are emitted as stages change.
    QString startRun( const AgentRunRequest &request );

    /// Convenience synchronous helper that blocks until the run is terminal.
    AgentRun runSynchronously( const AgentRunRequest &request );

    /// Request cancellation of the active (or most recently started) run.
    void cancelRun( const QString &runId );

    /// Snapshot of a run record.
    AgentRun runState( const QString &runId ) const;

  signals:
    void runStageChanged( const sicnu::agent::AgentRun &run );
    void runCompleted( const sicnu::agent::AgentRun &run );
    void runFailed( const sicnu::agent::AgentRun &run );
    void runCanceled( const sicnu::agent::AgentRun &run );

  private:
    AgentRun executeRun( AgentRun run );
    void transitionStage( AgentRun &run, AgentRunStage stage );
    void emitTerminalSignal( const AgentRun &run );
    void installDefaultFunctions();

    PreflightFunction m_preflightFunction;
    ExecuteFunction m_executeFunction;
    VerifyFunction m_verifyFunction;
    RepairFunction m_repairFunction;
    PresentFunction m_presentFunction;

    sicnu::data::DataManager *m_dataManager = nullptr;

    mutable QMutex m_mutex;
    QMap<QString, AgentRun> m_runs;
    QString m_activeRunId;
    std::atomic<bool> m_cancelRequested{ false };
    std::function<void()> m_currentCancel;
};

} // namespace sicnu::agent

Q_DECLARE_METATYPE( sicnu::agent::AgentRun )
Q_DECLARE_METATYPE( sicnu::agent::AgentRunStage )

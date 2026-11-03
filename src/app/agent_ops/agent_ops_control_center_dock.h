/***************************************************************************
 * agent_ops_control_center_dock.h — Control Center Dock (Milestone 4)
 *
 * QDockWidget encapsulating AgentOpsControlCenterPanel.
 * Worker lifecycle (#1540, #1742/AS-006): ONE persistent serialized executor
 * thread. startWorker() cancels the running task and REPLACES the queued
 * one; tasks never overlap, so the coordinator's single-thread state
 * (approvals, lastResult) is touched by exactly one thread at a time, while
 * startWorker() itself never blocks the GUI thread (no join on the caller).
 * The destructor cancels, drops pending work and joins exactly once.
 * Repair approval (#1733/#1760): the operator's Approve click records an
 * intent; the executor thread MINTS the HMAC token via the coordinator's
 * human channel and arms it immediately before the launch that consumes it,
 * surfacing every typed refusal into the panel.
 ***************************************************************************/
#pragma once

#include "app/agent_ops/agent_ops_control_center_panel.h"
#include "agent_ops/operations_coordinator.h"
#include "agent_ops/ops_driver.h"
#include "agent_ops/ops_projection.h"
#include "agent_ops/ops_types.h"

#include <QDockWidget>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class QCloseEvent;

namespace sicnu::app::agent_ops {

class AgentOpsControlCenterDock : public QDockWidget
{
    Q_OBJECT

  public:
    explicit AgentOpsControlCenterDock(QWidget *parent = nullptr);
    explicit AgentOpsControlCenterDock(const QString &title, QWidget *parent = nullptr);
    ~AgentOpsControlCenterDock() override;

    AgentOpsControlCenterPanel *panel() const { return m_panel; }

    void setDriver(sicnu::agent_ops::OpsDriver *driver);
    sicnu::agent_ops::OpsDriver *driver() const { return m_driver; }

    void setCoordinator(sicnu::agent_ops::OperationsCoordinator *coordinator);
    sicnu::agent_ops::OperationsCoordinator *coordinator() const;

    bool isWorkerRunning() const { return mWorkerRunning.load(); }
    bool isCancelRequested() const
    {
        if (mCurrentWorkerCancel && mCurrentWorkerCancel->load())
            return true;
        return mCancelRequested.load();
    }
    bool isPauseRequested() const;

    void startSession(const sicnu::agent_ops::OpsRunRequest &request);
    void startResume(const std::string &journalDirectory,
                     const std::string &sessionId,
                     const sicnu::agent_ops::OpsRunRequest &request);

    /// Queues `task` on the serialized executor. The currently running task
    /// (if any) gets its cancel flag set and FINISHES before this task
    /// starts — tasks never overlap (#1742). Never blocks the caller.
    void startWorker(std::function<void(const std::atomic<bool> &cancelRequested)> task);

    /// Barrier: returns when no task is running and none is queued. The
    /// executor thread stays alive for later startWorker calls.
    void joinWorker();

    /// Remembers a pre-minted repair-approval token for the NEXT launch and
    /// validates its SHAPE right here (#1760: garbage is a typed refusal
    /// now, not a deferred qWarning). The MAC/binding verification happens
    /// on the coordinator thread inside the launch that arms it; both the
    /// immediate and the deferred verdicts reach approvalStatusChanged().
    std::string armRepairApproval(const Json::Value &tokenDoc, long long nowMs);

    void setProjection(const sicnu::agent_ops::OpsProjection &projection);
    void setDelivery(const sicnu::agent_ops::FinalDelivery &delivery);
    void setCurrentDecisionJson(const QString &json);

  public slots:
    void requestPause();
    void requestCancel();
    void requestResume();
    void requestApproveRepair();
    void requestExport();

  signals:
    void pauseRequested();
    void cancelRequested();
    void resumeRequested();
    void approveRepairRequested();
    void exportRequested();
    void sessionFinished(bool ok);
    /// Human-gate visibility (#1760): every approval state change —
    /// accepted-for-launch, armed, or a typed refusal (mint or arm) — is
    /// reported here and mirrored into the panel.
    void approvalStatusChanged(const QString &status);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private slots:
    void onPauseRequested();
    void onCancelRequested();
    void onResumeRequested();
    void onApproveRepairRequested();
    void onExportRequested();

  private:
    void ensureWorkerThread();
    void publishApprovalStatus(const QString &status);

    AgentOpsControlCenterPanel *m_panel = nullptr;
    sicnu::agent_ops::OpsDriver *m_driver = nullptr;
    sicnu::agent_ops::OperationsCoordinator *m_coordinator = nullptr;

    // ── Serialized executor (#1742) ──────────────────────────────────────
    std::thread mWorkerThread;
    std::mutex mTaskMutex;
    std::condition_variable mTaskCv;
    std::function<void(const std::atomic<bool> &)> mQueuedTask;
    std::shared_ptr<std::atomic<bool>> mQueuedCancel;
    std::shared_ptr<std::atomic<bool>> mCurrentWorkerCancel; // running-or-queued task's flag
    bool mTaskQueued = false;
    bool mWorkerQuit = false;
    std::atomic<bool> mCancelRequested{false};
    std::atomic<bool> mWorkerRunning{false};

    // ── Repair approval (human gate) ─────────────────────────────────────
    Json::Value m_pendingRepairApprovalToken;
    /// The operator clicked Approve: the executor mints a fresh token via
    /// the coordinator's human channel right before the next launch.
    bool mOperatorApprovalIntent = false;
    std::string m_lastApprovalResult;
};

} // namespace sicnu::app::agent_ops

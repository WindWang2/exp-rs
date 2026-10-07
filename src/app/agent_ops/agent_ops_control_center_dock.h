/***************************************************************************
 * agent_ops_control_center_dock.h — Control Center Dock (Milestone 4)
 *
 * QDockWidget encapsulating AgentOpsControlCenterPanel.
 * Enforces strict background thread RAII lifetime and cancellation:
 * destructor cancels and joins mWorkerThread with 0 thread leaks, 0 UAFs.
 ***************************************************************************/
#pragma once

#include "app/agent_ops/agent_ops_control_center_panel.h"
#include "agent_ops/operations_coordinator.h"
#include "agent_ops/ops_driver.h"
#include "agent_ops/ops_projection.h"
#include "agent_ops/ops_types.h"

#include <QDockWidget>

#include <atomic>
#include <functional>
#include <memory>
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
    bool isCancelRequested() const { return mCancelRequested.load(); }
    bool isPauseRequested() const;

    void startSession(const sicnu::agent_ops::OpsRunRequest &request);
    void startResume(const std::string &journalDirectory,
                     const std::string &sessionId,
                     const sicnu::agent_ops::OpsRunRequest &request);

    void startWorker(std::function<void(const std::atomic<bool> &cancelRequested)> task);
    void joinWorker();

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

  protected:
    void closeEvent(QCloseEvent *event) override;

  private slots:
    void onPauseRequested();
    void onCancelRequested();
    void onResumeRequested();
    void onApproveRepairRequested();
    void onExportRequested();

  private:
    AgentOpsControlCenterPanel *m_panel = nullptr;
    sicnu::agent_ops::OpsDriver *m_driver = nullptr;
    sicnu::agent_ops::OperationsCoordinator *m_coordinator = nullptr;

    std::thread mWorkerThread;
    std::atomic<bool> mCancelRequested{false};
    std::atomic<bool> mWorkerRunning{false};

    Json::Value m_pendingRepairApprovalToken;
    std::string m_lastApprovalResult;
};

} // namespace sicnu::app::agent_ops

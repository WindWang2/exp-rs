/***************************************************************************
 * agent_ops_control_center_dock.cpp — Control Center Dock (Milestone 4)
 ***************************************************************************/
#include "app/agent_ops/agent_ops_control_center_dock.h"

#include <QCloseEvent>
#include <QMetaObject>
#include <QThread>

namespace sicnu::app::agent_ops {

AgentOpsControlCenterDock::AgentOpsControlCenterDock(QWidget *parent)
    : AgentOpsControlCenterDock(tr("Agent Operations Control Center"), parent)
{
}

AgentOpsControlCenterDock::AgentOpsControlCenterDock(const QString &title, QWidget *parent)
    : QDockWidget(title.isEmpty() ? tr("Agent Operations Control Center") : title, parent)
{
    setObjectName(QStringLiteral("AgentOpsControlCenterDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    m_panel = new AgentOpsControlCenterPanel(this);
    setWidget(m_panel);

    connect(m_panel, &AgentOpsControlCenterPanel::pauseRequested,
            this, &AgentOpsControlCenterDock::onPauseRequested);
    connect(m_panel, &AgentOpsControlCenterPanel::cancelRequested,
            this, &AgentOpsControlCenterDock::onCancelRequested);
    connect(m_panel, &AgentOpsControlCenterPanel::resumeRequested,
            this, &AgentOpsControlCenterDock::onResumeRequested);
    connect(m_panel, &AgentOpsControlCenterPanel::approveRepairRequested,
            this, &AgentOpsControlCenterDock::onApproveRepairRequested);
    connect(m_panel, &AgentOpsControlCenterPanel::exportRequested,
            this, &AgentOpsControlCenterDock::onExportRequested);
}

AgentOpsControlCenterDock::~AgentOpsControlCenterDock()
{
    mCancelRequested.store(true);
    if (auto *coord = coordinator())
    {
        coord->requestCancel();
    }
    if (mWorkerThread.joinable())
    {
        mWorkerThread.join();
    }
}

void AgentOpsControlCenterDock::setDriver(sicnu::agent_ops::OpsDriver *driver)
{
    m_driver = driver;
}

void AgentOpsControlCenterDock::setCoordinator(sicnu::agent_ops::OperationsCoordinator *coordinator)
{
    m_coordinator = coordinator;
}

sicnu::agent_ops::OperationsCoordinator *AgentOpsControlCenterDock::coordinator() const
{
    if (m_coordinator)
    {
        return m_coordinator;
    }
    if (m_driver)
    {
        return &m_driver->coordinator();
    }
    return nullptr;
}

bool AgentOpsControlCenterDock::isPauseRequested() const
{
    if (auto *coord = coordinator())
    {
        return coord->isPauseRequested();
    }
    return false;
}

void AgentOpsControlCenterDock::closeEvent(QCloseEvent *event)
{
    requestCancel();
    QDockWidget::closeEvent(event);
}

void AgentOpsControlCenterDock::requestPause()
{
    if (auto *coord = coordinator())
    {
        coord->requestPause();
    }
    emit pauseRequested();
}

void AgentOpsControlCenterDock::requestCancel()
{
    mCancelRequested.store(true);
    if (auto *coord = coordinator())
    {
        coord->requestCancel();
    }
    emit cancelRequested();
}

void AgentOpsControlCenterDock::requestResume()
{
    if (auto *coord = coordinator())
    {
        coord->clearPause();
    }
    emit resumeRequested();
}

void AgentOpsControlCenterDock::requestApproveRepair()
{
    if (!m_pendingRepairApprovalToken.isNull())
    {
        armRepairApproval(m_pendingRepairApprovalToken, 0);
    }
    emit approveRepairRequested();
}

void AgentOpsControlCenterDock::requestExport()
{
    emit exportRequested();
}

void AgentOpsControlCenterDock::onPauseRequested()
{
    requestPause();
}

void AgentOpsControlCenterDock::onCancelRequested()
{
    requestCancel();
}

void AgentOpsControlCenterDock::onResumeRequested()
{
    requestResume();
}

void AgentOpsControlCenterDock::onApproveRepairRequested()
{
    requestApproveRepair();
}

void AgentOpsControlCenterDock::onExportRequested()
{
    requestExport();
}

void AgentOpsControlCenterDock::startWorker(std::function<void(const std::atomic<bool> &cancelRequested)> task)
{
    if (mWorkerThread.joinable())
    {
        mCancelRequested.store(true);
        if (auto *coord = coordinator())
        {
            coord->requestCancel();
        }
        mWorkerThread.join();
    }

    mCancelRequested.store(false);
    mWorkerRunning.store(true);

    mWorkerThread = std::thread([this, task = std::move(task)]() {
        if (task)
        {
            task(mCancelRequested);
        }
        mWorkerRunning.store(false);
    });
}

void AgentOpsControlCenterDock::joinWorker()
{
    if (mWorkerThread.joinable())
    {
        mWorkerThread.join();
    }
}

void AgentOpsControlCenterDock::startSession(const sicnu::agent_ops::OpsRunRequest &request)
{
    auto *coord = coordinator();
    if (!coord)
    {
        return;
    }
    coord->clearCancel();
    startWorker([this, coord, request](const std::atomic<bool> &) {
        sicnu::agent_ops::OpsRunResult res = coord->run(request);
        QMetaObject::invokeMethod(this, [this, res]() {
            if (m_panel)
            {
                m_panel->setProjection(res.projection);
                m_panel->setDelivery(res.delivery);
            }
            emit sessionFinished(res.ok);
        }, Qt::QueuedConnection);
    });
}

void AgentOpsControlCenterDock::startResume(const std::string &journalDirectory,
                                            const std::string &sessionId,
                                            const sicnu::agent_ops::OpsRunRequest &request)
{
    auto *coord = coordinator();
    if (!coord)
    {
        return;
    }
    coord->clearCancel();
    startWorker([this, coord, journalDirectory, sessionId, request](const std::atomic<bool> &) {
        sicnu::agent_ops::OpsRunResult res = coord->resume(journalDirectory, sessionId, request);
        QMetaObject::invokeMethod(this, [this, res]() {
            if (m_panel)
            {
                m_panel->setProjection(res.projection);
                m_panel->setDelivery(res.delivery);
            }
            emit sessionFinished(res.ok);
        }, Qt::QueuedConnection);
    });
}

std::string AgentOpsControlCenterDock::armRepairApproval(const Json::Value &tokenDoc, long long nowMs)
{
    m_pendingRepairApprovalToken = tokenDoc;
    if (auto *coord = coordinator())
    {
        m_lastApprovalResult = coord->armRepairApproval(tokenDoc, nowMs);
        return m_lastApprovalResult;
    }
    m_lastApprovalResult = "NO_COORDINATOR";
    return m_lastApprovalResult;
}

void AgentOpsControlCenterDock::setProjection(const sicnu::agent_ops::OpsProjection &projection)
{
    if (!m_panel)
        return;
    if (QThread::currentThread() == thread())
    {
        m_panel->setProjection(projection);
    }
    else
    {
        QMetaObject::invokeMethod(m_panel, [panel = m_panel, projection]() {
            panel->setProjection(projection);
        }, Qt::QueuedConnection);
    }
}

void AgentOpsControlCenterDock::setDelivery(const sicnu::agent_ops::FinalDelivery &delivery)
{
    if (!m_panel)
        return;
    if (QThread::currentThread() == thread())
    {
        m_panel->setDelivery(delivery);
    }
    else
    {
        QMetaObject::invokeMethod(m_panel, [panel = m_panel, delivery]() {
            panel->setDelivery(delivery);
        }, Qt::QueuedConnection);
    }
}

void AgentOpsControlCenterDock::setCurrentDecisionJson(const QString &json)
{
    if (!m_panel)
        return;
    if (QThread::currentThread() == thread())
    {
        m_panel->setCurrentDecisionJson(json);
    }
    else
    {
        QMetaObject::invokeMethod(m_panel, [panel = m_panel, json]() {
            panel->setCurrentDecisionJson(json);
        }, Qt::QueuedConnection);
    }
}

} // namespace sicnu::app::agent_ops

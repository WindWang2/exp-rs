/***************************************************************************
 * agent_ops_control_center_dock.cpp — Control Center Dock (Milestone 4)
 ***************************************************************************/
#include "app/agent_ops/agent_ops_control_center_dock.h"

#include <QCloseEvent>
#include <QDateTime>
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
    m_aliveFlag->store( false, std::memory_order_release );
    mCancelRequested.store( true );
    if ( mCurrentWorkerCancel )
    {
        mCurrentWorkerCancel->store( true );
    }
    if ( auto *coord = coordinator() )
    {
        coord->requestCancel();
    }
    if ( mWorkerThread.joinable() )
    {
        mWorkerThread.join();
    }
    std::lock_guard<std::mutex> lock( m_reaperMutex );
    for ( auto &w : m_retiredWorkers )
    {
        if ( w.thread.joinable() )
            w.thread.join();
    }
    m_retiredWorkers.clear();
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
    mCancelRequested.store( true );
    if ( mCurrentWorkerCancel )
    {
        mCurrentWorkerCancel->store( true );
    }
    if ( auto *coord = coordinator() )
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
    // Nothing is armed from here: the coordinator's approval state (armed
    // approval, consumed digests, last projection, lastResult) belongs to
    // the coordinator thread — the worker inside run()/resume() — so arming
    // it on the GUI thread would race the in-flight session (#1540; the
    // contract in operations_coordinator.h). The token remembered by
    // armRepairApproval() is armed by the worker at the next launch
    // (startSession), which is also the launch that consumes it.
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

void AgentOpsControlCenterDock::pruneReaperThreadsLocked()
{
    for ( auto it = m_retiredWorkers.begin(); it != m_retiredWorkers.end(); )
    {
        if ( it->done && it->done->load( std::memory_order_acquire ) )
        {
            if ( it->thread.joinable() )
                it->thread.join();
            it = m_retiredWorkers.erase( it );
        }
        else
        {
            ++it;
        }
    }
}

void AgentOpsControlCenterDock::startWorker(std::function<void(const std::atomic<bool> &cancelRequested)> task)
{
    if (mWorkerThread.joinable())
    {
        mCancelRequested.store(true);
        if (mCurrentWorkerCancel)
        {
            mCurrentWorkerCancel->store(true);
        }
        if (auto *coord = coordinator())
        {
            coord->requestCancel();
        }
        std::lock_guard<std::mutex> lock( m_reaperMutex );
        pruneReaperThreadsLocked();
        auto done = std::make_shared<std::atomic<bool>>( false );
        std::thread reaper( [t = std::move( mWorkerThread ), done]() mutable {
            if ( t.joinable() )
                t.join();
            done->store( true, std::memory_order_release );
        } );
        m_retiredWorkers.push_back( { std::move( reaper ), done } );
    }

    auto taskCancel = std::make_shared<std::atomic<bool>>(false);
    mCurrentWorkerCancel = taskCancel;
    mCancelRequested.store(false);
    const uint64_t gen = ++mWorkerGeneration;
    mWorkerRunning.store(true);

    mWorkerThread = std::thread([this, alive = m_aliveFlag, taskCancel, gen, task = std::move(task)]() {
        if (task)
        {
            task(*taskCancel);
        }
        if (!alive->load(std::memory_order_acquire))
            return;
        if (mWorkerGeneration.load() == gen)
        {
            mWorkerRunning.store(false);
        }
    });
}

void AgentOpsControlCenterDock::joinWorker()
{
    if (mWorkerThread.joinable())
    {
        mWorkerThread.join();
    }
    std::lock_guard<std::mutex> lock( m_reaperMutex );
    for ( auto &w : m_retiredWorkers )
    {
        if ( w.thread.joinable() )
            w.thread.join();
    }
    m_retiredWorkers.clear();
}

void AgentOpsControlCenterDock::startSession(const sicnu::agent_ops::OpsRunRequest &request)
{
    auto *coord = coordinator();
    if (!coord)
    {
        return;
    }
    coord->clearCancel();
    // Hand the remembered approval token to the worker BY VALUE: the copy
    // happens on this (GUI) thread before the worker starts, and the worker
    // arms it on the coordinator thread immediately before the run() that
    // consumes it. Arming it here on the GUI thread would race the in-flight
    // session's run()/finish() — the coordinator's approval state is
    // coordinator-thread state (#1540; contract in operations_coordinator.h).
    const Json::Value approvalToken = m_pendingRepairApprovalToken;
    startWorker([this, coord, request, approvalToken](const std::atomic<bool> &) {
        if (!approvalToken.isNull())
        {
            // The verifier treats nowMs <= 0 as an unusable clock and fails
            // closed (expired), so a zero clock could never arm an approval —
            // arm at the real wall clock. (#1451)
            // The typed refusal must not vanish: qWarning reaches the
            // application message log, so the operator sees why the approval
            // did not arm. (#1451)
            const std::string error = coord->armRepairApproval(
                approvalToken, QDateTime::currentMSecsSinceEpoch());
            if (!error.empty())
                qWarning("agent ops: repair approval not armed: %s", error.c_str());
        }
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
    // Remember the token; the worker thread arms it on the coordinator at the
    // next launch (see startSession). The coordinator's approval state
    // (armed approval, consumed digests, last projection, lastResult)
    // belongs to the coordinator thread — the thread inside run()/resume(),
    // i.e. this dock's worker — so calling coord->armRepairApproval() here on
    // the GUI thread would race the in-flight session's run()/finish()
    // (#1540; contract in operations_coordinator.h). The typed refusal
    // therefore surfaces from the worker at launch (qWarning) rather than
    // from this call, and the launch-time wall clock is the verification
    // clock (nowMs <= 0 would fail closed as an unusable clock anyway).
    (void)nowMs;
    m_pendingRepairApprovalToken = tokenDoc;
    m_lastApprovalResult.clear();
    return {};
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

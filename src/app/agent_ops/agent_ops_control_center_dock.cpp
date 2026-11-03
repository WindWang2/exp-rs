/***************************************************************************
 * agent_ops_control_center_dock.cpp — Control Center Dock (Milestone 4)
 ***************************************************************************/
#include "app/agent_ops/agent_ops_control_center_dock.h"

#include "agent_ops/repair_approval.h"

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
    connect(this, &AgentOpsControlCenterDock::approvalStatusChanged,
            m_panel, &AgentOpsControlCenterPanel::setApprovalStatus);
}

AgentOpsControlCenterDock::~AgentOpsControlCenterDock()
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
    {
        std::lock_guard<std::mutex> lock( mTaskMutex );
        mWorkerQuit = true;
        mQueuedTask = nullptr; // pending work is dropped, never started, never dangling
        mTaskQueued = false;
    }
    mTaskCv.notify_all();
    if ( mWorkerThread.joinable() )
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
    // The human gate (#1733): the click records OPERATOR INTENT on the GUI
    // thread; the token itself is minted by the coordinator (process-local
    // HMAC secret) on the executor thread immediately before the launch
    // that consumes it — the coordinator's approval state is never touched
    // from the GUI thread (#1540 contract in operations_coordinator.h), and
    // no model-reachable surface can record this intent.
    mOperatorApprovalIntent = true;
    publishApprovalStatus(tr("operator approval recorded; it will be minted and armed at the next launch"));
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

void AgentOpsControlCenterDock::ensureWorkerThread()
{
    // Serialized executor (#1742/AS-006): exactly one thread, one task at a
    // time. A retiring task's coordinator state writes are strictly ordered
    // before the next task's — the overlapping-worker race (two tasks inside
    // run()/armRepairApproval at once) is structurally impossible now.
    if ( mWorkerThread.joinable() )
    {
        return;
    }
    mWorkerThread = std::thread( [this]() {
        for ( ;; )
        {
            std::function<void( const std::atomic<bool> & )> task;
            std::shared_ptr<std::atomic<bool>> cancel;
            {
                std::unique_lock<std::mutex> lock( mTaskMutex );
                mTaskCv.wait( lock, [this]() { return mWorkerQuit || mTaskQueued; } );
                if ( mWorkerQuit )
                    return; // pending work was dropped by the quitter
                task = std::move( mQueuedTask );
                cancel = mQueuedCancel;
                mQueuedTask = nullptr;
                mQueuedCancel.reset();
                mTaskQueued = false;
                // Inside the dequeue critical section (R1-F2): marking the
                // task running BEFORE releasing the mutex closes the window
                // where joinWorker()'s !queued && !running predicate could
                // observe "idle" between the dequeue and the task start.
                mWorkerRunning.store( true );
            }
            if ( task )
            {
                task( *cancel );
            }
            {
                // Under the mutex + followed by notify: joinWorker()'s
                // predicate watches this flag, and a cv predicate state
                // change without a matching notification could otherwise
                // park the barrier past the moment it became true.
                std::lock_guard<std::mutex> lock( mTaskMutex );
                mWorkerRunning.store( false );
            }
            mTaskCv.notify_all();
        }
    } );
}

void AgentOpsControlCenterDock::startWorker(std::function<void(const std::atomic<bool> &cancelRequested)> task)
{
    // Cancel the live task (if any): cooperative — it observes its flag and
    // exits; the queued replacement runs strictly after it returns.
    mCancelRequested.store(true);
    if (mCurrentWorkerCancel)
    {
        mCurrentWorkerCancel->store(true);
    }
    if (auto *coord = coordinator())
    {
        coord->requestCancel();
    }
    auto taskCancel = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock( mTaskMutex );
        // One-deep queue: only the LATEST requested task matters; an older
        // never-started task is replaced (its cancel flag is set so a stale
        // shared_ptr holder still observes cancellation).
        if ( mQueuedCancel )
        {
            mQueuedCancel->store( true );
        }
        mQueuedTask = std::move(task);
        mQueuedCancel = taskCancel;
        mTaskQueued = true;
    }
    mCurrentWorkerCancel = taskCancel;
    mCancelRequested.store(false);
    ensureWorkerThread();
    mTaskCv.notify_all();
}

void AgentOpsControlCenterDock::joinWorker()
{
    // Barrier over the serialized executor: wait until nothing runs and
    // nothing is queued. The executor thread itself stays alive.
    std::unique_lock<std::mutex> lock( mTaskMutex );
    mTaskCv.wait( lock, [this]() { return !mTaskQueued && !mWorkerRunning.load(); } );
}

void AgentOpsControlCenterDock::startSession(const sicnu::agent_ops::OpsRunRequest &request)
{
    auto *coord = coordinator();
    if (!coord)
    {
        return;
    }
    coord->clearCancel();
    // Snapshot the human-gate inputs on the GUI thread; the worker mints
    // and arms on the coordinator thread immediately before the run() that
    // consumes them (#1733, #1540).
    const Json::Value approvalToken = m_pendingRepairApprovalToken;
    const bool mintForOperator = mOperatorApprovalIntent;
    m_pendingRepairApprovalToken = Json::Value();
    mOperatorApprovalIntent = false;
    startWorker([this, coord, request, approvalToken, mintForOperator](const std::atomic<bool> &) {
        Json::Value approval = approvalToken;
        if (mintForOperator)
        {
            std::string mintError;
            approval = coord->mintRepairApprovalForOperator(
                QDateTime::currentMSecsSinceEpoch(), 0, &mintError);
            if (approval.isNull())
            {
                const QString status = tr("repair approval not minted: %1")
                                           .arg(QString::fromStdString(mintError));
                qWarning("agent ops: %s", status.toUtf8().constData());
                QMetaObject::invokeMethod(this, [this, status]() {
                    emit approvalStatusChanged(status);
                }, Qt::QueuedConnection);
            }
        }
        if (!approval.isNull())
        {
            // The verifier treats nowMs <= 0 as an unusable clock and fails
            // closed (expired), so arm at the real wall clock (#1451). The
            // typed refusal is surfaced into the panel, not only the log
            // (#1760).
            const std::string error = coord->armRepairApproval(
                approval, QDateTime::currentMSecsSinceEpoch());
            if (!error.empty())
            {
                const QString status = tr("repair approval not armed: %1")
                                           .arg(QString::fromStdString(error));
                qWarning("agent ops: %s", status.toUtf8().constData());
                QMetaObject::invokeMethod(this, [this, status]() {
                    emit approvalStatusChanged(status);
                }, Qt::QueuedConnection);
            }
            else
            {
                QMetaObject::invokeMethod(this, [this]() {
                    emit approvalStatusChanged(tr("repair approval armed for this launch"));
                }, Qt::QueuedConnection);
            }
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
    // #1760 (AS-008): a garbage token is refused NOW, on the caller's
    // thread, with the typed code — the old shape stored anything and
    // reported success unconditionally. The full MAC/binding verification
    // still happens on the coordinator thread inside the launch that arms
    // the token (shape alone proves nothing about authenticity), and its
    // verdict reaches the panel through approvalStatusChanged.
    (void)nowMs; // the launch-time wall clock is the verification clock
    const char *shape = sicnu::agent_ops::validateRepairApprovalTokenShape(tokenDoc);
    if (std::string(shape) != sicnu::agent_ops::approval_check::kOk)
    {
        const std::string error =
            std::string("APPROVAL_") +
            (std::string(shape) == std::string(sicnu::agent_ops::approval_check::kTtlExcessive)
                 ? "TTL_EXCESSIVE"
                 : "MALFORMED");
        m_lastApprovalResult = error;
        publishApprovalStatus(tr("repair approval refused: %1").arg(QString::fromStdString(error)));
        return error;
    }
    m_pendingRepairApprovalToken = tokenDoc;
    m_lastApprovalResult.clear();
    publishApprovalStatus(tr("repair approval accepted; it will be armed at the next launch"));
    return {};
}

void AgentOpsControlCenterDock::publishApprovalStatus(const QString &status)
{
    if (QThread::currentThread() == thread())
    {
        m_lastApprovalResult = status.toStdString();
        emit approvalStatusChanged(status);
        return;
    }
    QMetaObject::invokeMethod(this, [this, status]() {
        emit approvalStatusChanged(status);
    }, Qt::QueuedConnection);
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

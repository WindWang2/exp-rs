// async_algorithm_runner.cpp — Helper for async algorithm execution in dialogs
#include "async_algorithm_runner.h"

#include <QDateTime>

#include <qgsapplication.h>
#include <qgsmessagelog.h>
#include <qgstaskmanager.h>
#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <processing/qgsprocessingalgrunnertask.h>
#include "processing/framework/task_center.h"

AsyncAlgorithmRunner::AsyncAlgorithmRunner(QWidget *parentWidget, QObject *parent)
    : AsyncRunnerBase(parentWidget, parent)
{
}

AsyncAlgorithmRunner::~AsyncAlgorithmRunner()
{
    // Cancel outstanding task so it does not call into a destroyed runner/dialog.
    if (m_task && isRunning()) {
        if (m_centerTaskId > 0) {
            sicnu::TaskCenter::instance().markTaskCanceled(m_centerTaskId);
        }
        disconnect(m_task, nullptr, this, nullptr);
        m_task->cancel();
        m_task = nullptr;
    }
}

void AsyncAlgorithmRunner::run(const QgsProcessingAlgorithm *algorithm,
                                const QVariantMap &parameters,
                                QgsProcessingContext &context)
{
    if (isRunning()) {
        QgsMessageLog::logMessage("Algorithm already running", "async_runner", Qgis::MessageLevel::Warning);
        return;
    }

    beginRun();
    m_startTime = QDateTime::currentMSecsSinceEpoch();

    QString algoId = algorithm ? algorithm->id() : QStringLiteral("algorithm");
    m_centerTaskId = sicnu::TaskCenter::instance().enqueueTask(algoId, parameters, true);
    long centerTaskId = m_centerTaskId;

    // Create feedback for progress tracking
    QgsProcessingFeedback *feedback = new QgsProcessingFeedback(this);

    // Create async task
    m_task = new QgsProcessingAlgRunnerTask(algorithm, parameters, context, feedback);
    sicnu::TaskCenter::instance().attachQgsTask(centerTaskId, m_task);
    sicnu::TaskCenter::instance().markTaskRunning(centerTaskId);

    // Connect to executed signal (bool successful, QVariantMap results).
    // m_task is stored as the QgsTask base pointer (testing seam); the signal
    // lives on the derived type, so the sender argument must be downcast.
    connect(static_cast<QgsProcessingAlgRunnerTask *>(m_task),
            &QgsProcessingAlgRunnerTask::executed, this,
            [this, feedback, centerTaskId](bool successful, const QVariantMap &results) {
        m_task = nullptr;
        endRun();

        double elapsed = (QDateTime::currentMSecsSinceEpoch() - m_startTime) / 1000.0;

        if (successful) {
            QgsMessageLog::logMessage(
                QObject::tr("Algorithm completed in %1 seconds").arg(elapsed, 0, 'f', 2),
                "async_runner", Qgis::MessageLevel::Success);
            sicnu::TaskCenter::instance().markTaskCompleted(centerTaskId, results);
            emit completed(results);
        } else {
            QString errorMsg = feedback ? feedback->textLog() : "Unknown error";
            QgsMessageLog::logMessage(
                QObject::tr("Algorithm failed after %1 seconds: %2").arg(elapsed, 0, 'f', 2).arg(errorMsg),
                "async_runner", Qgis::MessageLevel::Critical);
            sicnu::TaskCenter::instance().markTaskFailed(centerTaskId, errorMsg);
            emit failed(errorMsg);
        }

        feedback->deleteLater();
    });

    connect(m_task, &QgsTask::progressChanged, this, [this, centerTaskId](double progress) {
        sicnu::TaskCenter::instance().updateTaskProgress(centerTaskId, progress / 100.0);
        emit progressChanged(progress);
    });

    // Add to task manager
    QgsApplication::taskManager()->addTask(m_task);
}

void AsyncAlgorithmRunner::cancel()
{
    if (m_task) {
        m_task->cancel();
    }
}

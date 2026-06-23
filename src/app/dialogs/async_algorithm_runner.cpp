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

AsyncAlgorithmRunner::AsyncAlgorithmRunner(QWidget *parentWidget, QObject *parent)
    : AsyncRunnerBase(parentWidget, parent)
{
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

    // Create feedback for progress tracking
    QgsProcessingFeedback *feedback = new QgsProcessingFeedback(this);

    // Create async task
    m_task = new QgsProcessingAlgRunnerTask(algorithm, parameters, context, feedback);

    // Connect to executed signal (bool successful, QVariantMap results)
    connect(m_task, &QgsProcessingAlgRunnerTask::executed, this,
            [this, feedback](bool successful, const QVariantMap &results) {
        m_task = nullptr;
        endRun();

        double elapsed = (QDateTime::currentMSecsSinceEpoch() - m_startTime) / 1000.0;

        if (successful) {
            QgsMessageLog::logMessage(
                QObject::tr("Algorithm completed in %1 seconds").arg(elapsed, 0, 'f', 2),
                "async_runner", Qgis::MessageLevel::Success);
            emit completed(results);
        } else {
            QString errorMsg = feedback ? feedback->textLog() : "Unknown error";
            QgsMessageLog::logMessage(
                QObject::tr("Algorithm failed after %1 seconds: %2").arg(elapsed, 0, 'f', 2).arg(errorMsg),
                "async_runner", Qgis::MessageLevel::Critical);
            emit failed(errorMsg);
        }

        feedback->deleteLater();
    });

    connect(m_task, &QgsTask::progressChanged, this, [this](double progress) {
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

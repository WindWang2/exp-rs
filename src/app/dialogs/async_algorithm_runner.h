// async_algorithm_runner.h — DEPRECATED: use JobEngine / SicnuAlgorithmDialog
// or RasterProcessingDialogBase::runAlgorithmTask instead.
#pragma once

#include "async_runner_base.h"
#include <QVariantMap>

class QgsProcessingAlgorithm;
class QgsProcessingContext;
class QgsProcessingFeedback;
class QgsProcessingAlgRunnerTask;

/**
 * \deprecated Prefer SicnuAlgorithmDialog (toolbox) or
 * RasterProcessingDialogBase::runAlgorithmTask() / JobEngine processing: executor.
 *
 * Kept as a thin legacy type for tests / transitional includes.
 */
class AsyncAlgorithmRunner : public AsyncRunnerBase
{
    Q_OBJECT

public:
    explicit AsyncAlgorithmRunner(QWidget *parentWidget, QObject *parent = nullptr);
    ~AsyncAlgorithmRunner() override;

    /** \deprecated Prefer JobEngine-backed paths. */
    void run(const QgsProcessingAlgorithm *algorithm,
             const QVariantMap &parameters,
             QgsProcessingContext &context);

    void cancel();

signals:
    void completed(const QVariantMap &results);
    void progressChanged(double progress);

protected:
    void setTaskForTesting(QgsProcessingAlgRunnerTask *task, long centerTaskId)
    {
        beginRun();
        m_task = task;
        m_centerTaskId = centerTaskId;
    }

private:
    QgsProcessingAlgRunnerTask *m_task = nullptr;
    qint64 m_startTime = 0;
    long m_centerTaskId = -1;
};

// async_algorithm_runner.h — Helper for async algorithm execution in dialogs
#pragma once

#include "async_runner_base.h"
#include <QVariantMap>

class QgsProcessingAlgorithm;
class QgsProcessingContext;
class QgsProcessingFeedback;
class QgsProcessingAlgRunnerTask;

/**
 * Helper class for running processing algorithms asynchronously in dialogs.
 *
 * Usage:
 *   AsyncAlgorithmRunner *runner = new AsyncAlgorithmRunner(this);
 *   connect(runner, &AsyncAlgorithmRunner::completed, this, &MyDialog::onAlgorithmCompleted);
 *   connect(runner, &AsyncAlgorithmRunner::failed, this, &MyDialog::onAlgorithmFailed);
 *   runner->run(algorithm, params, context);
 */
class AsyncAlgorithmRunner : public AsyncRunnerBase
{
    Q_OBJECT

public:
    explicit AsyncAlgorithmRunner(QWidget *parentWidget, QObject *parent = nullptr);

    /**
     * Run an algorithm asynchronously.
     * The algorithm is cloned internally, so the caller retains ownership of the original.
     */
    void run(const QgsProcessingAlgorithm *algorithm,
             const QVariantMap &parameters,
             QgsProcessingContext &context);

    /**
     * Cancel the running task.
     */
    void cancel();

signals:
    /** Emitted when the algorithm completes successfully. */
    void completed(const QVariantMap &results);

    /** Emitted with progress updates (0-100). */
    void progressChanged(double progress);

private:
    QgsProcessingAlgRunnerTask *m_task = nullptr;
    qint64 m_startTime = 0;
};

// async_gdal_runner.h — Reusable async GDAL I/O runner for dialogs
#pragma once

#include "async_runner_base.h"
#include <QFuture>
#include <QFutureWatcher>
#include <functional>

/**
 * Helper class for running GDAL operations asynchronously in dialogs.
 *
 * Usage:
 *   auto *runner = new AsyncGdalRunner(this);
 *   connect(runner, &AsyncGdalRunner::completed, this, [this](const QString &outputPath) {
 *       loadRasterLayer(outputPath);
 *       accept();
 *   });
 *   connect(runner, &AsyncGdalRunner::failed, this, [this](const QString &error) {
 *       QMessageBox::warning(this, tr("Error"), error);
 *   });
 *   runner->run(task);
 */
class AsyncGdalRunner : public AsyncRunnerBase
{
    Q_OBJECT

public:
    explicit AsyncGdalRunner(QWidget *parentWidget, QObject *parent = nullptr);
    ~AsyncGdalRunner() override;

    /**
     * Task functor executed on a background thread.
     *
     * Return contract:
     *   - non-empty path  → success (completed signal)
     *   - empty string    → generic failure
     *   - string starting with errorMarker() → failure with message after the marker
     */
    using GdalTask = std::function<QString()>;

    /** Prefix for structured error returns from background tasks. */
    static QString errorMarker() { return QStringLiteral("\x01SICNU_ERR\x01"); }

    /**
     * Run a GDAL/operator task asynchronously.
     * The task function is executed on a background thread.
     */
    void run(const GdalTask &task);

signals:
    /** Emitted when the task completes successfully. Output path is provided. */
    void completed(const QString &outputPath);

private:
    QFutureWatcher<QString> *m_watcher = nullptr;
};

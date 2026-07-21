// async_gdal_runner.h — DEPRECATED: use RasterProcessingDialogBase::runGdalTask
// (JobEngine callable:gdal_task) instead.
#pragma once

#include "async_runner_base.h"
#include <QFuture>
#include <QFutureWatcher>
#include <functional>

/**
 * \deprecated Prefer RasterProcessingDialogBase::runGdalTask() which submits
 * to JobEngine so the job appears in RsJobPanel.
 *
 * Kept as a thin legacy type for tests / transitional includes.
 */
class AsyncGdalRunner : public AsyncRunnerBase
{
    Q_OBJECT

public:
    explicit AsyncGdalRunner(QWidget *parentWidget, QObject *parent = nullptr);
    ~AsyncGdalRunner() override;

    using GdalTask = std::function<QString()>;

    static QString errorMarker() { return QStringLiteral("\x01SICNU_ERR\x01"); }

    /** \deprecated Routes through QtConcurrent only — prefer runGdalTask/JobEngine. */
    void run(const GdalTask &task);

signals:
    void completed(const QString &outputPath);

private:
    QFutureWatcher<QString> *m_watcher = nullptr;
};

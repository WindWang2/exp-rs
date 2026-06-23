// async_gdal_runner.cpp — Reusable async GDAL I/O runner for dialogs
#include "async_gdal_runner.h"

#include <QtConcurrent>

AsyncGdalRunner::AsyncGdalRunner(QWidget *parentWidget, QObject *parent)
    : AsyncRunnerBase(parentWidget, parent)
{
}

AsyncGdalRunner::~AsyncGdalRunner()
{
    if (m_watcher && isRunning()) {
        m_watcher->cancel();
        m_watcher->waitForFinished();
    }
}

void AsyncGdalRunner::run(const GdalTask &task)
{
    if (isRunning()) return;

    beginRun();

    if (!m_watcher) {
        m_watcher = new QFutureWatcher<QString>(this);
        connect(m_watcher, &QFutureWatcher<QString>::finished, this, [this]() {
            endRun();

            QString result = m_watcher->result();
            if (result.isEmpty())
                emit failed(tr("Operation failed. Check log for details."));
            else
                emit completed(result);
        });
    }

    m_watcher->setFuture(QtConcurrent::run(task));
}

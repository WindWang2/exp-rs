// async_gdal_runner.cpp — Reusable async GDAL I/O runner for dialogs
#include "async_gdal_runner.h"

#include <QPointer>
#include <QtConcurrent>

AsyncGdalRunner::AsyncGdalRunner(QWidget *parentWidget, QObject *parent)
    : AsyncRunnerBase(parentWidget, parent)
{
}

AsyncGdalRunner::~AsyncGdalRunner()
{
    if (!m_watcher)
        return;

    // Disconnect before waiting so finished handlers cannot touch a dying dialog.
    disconnect(m_watcher, nullptr, this, nullptr);
    if (isRunning()) {
        m_watcher->cancel();
        m_watcher->setParent(nullptr);
        connect(m_watcher, &QFutureWatcher<QString>::finished, m_watcher, &QObject::deleteLater);
        m_watcher = nullptr;
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

            // Parent dialog may already be gone.
            const QPointer<QWidget> dialogGuard( m_parentWidget );
            if ( !dialogGuard )
                return;

            QString result = m_watcher->result();
            const QString marker = errorMarker();
            if (result.startsWith(marker))
                emit failed(result.mid(marker.size()));
            else if (result.isEmpty())
                emit failed(tr("Operation failed. Check log for details."));
            else
                emit completed(result);
        });
    }

    m_watcher->setFuture(QtConcurrent::run(task));
}

// async_runner_base.h — Base class for async runners with cursor management
#pragma once

#include <QObject>
#include <QString>
#include <QWidget>

/**
 * Base class for async runners providing common cursor management and running state.
 *
 * Subclasses implement specific run() methods and add their own completed() signals.
 */
class AsyncRunnerBase : public QObject
{
    Q_OBJECT

public:
    explicit AsyncRunnerBase(QWidget *parentWidget, QObject *parent = nullptr)
        : QObject(parent), m_parentWidget(parentWidget) {}

    /**
     * Check if a task is currently running.
     */
    bool isRunning() const { return m_running; }

signals:
    /** Emitted when the task fails. Error message is provided. */
    void failed(const QString &errorMessage);

protected:
    /**
     * Called at the start of run(). Sets running state and shows wait cursor.
     */
    void beginRun()
    {
        m_running = true;
        if (m_parentWidget)
            QApplication::setOverrideCursor(Qt::WaitCursor);
    }

    /**
     * Called at the end of run(). Clears running state and restores cursor.
     */
    void endRun()
    {
        m_running = false;
        if (m_parentWidget)
            QApplication::restoreOverrideCursor();
    }

    QWidget *m_parentWidget = nullptr;
    bool m_running = false;
};

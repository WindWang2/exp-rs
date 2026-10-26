// async_runner_base.h — Base class for async runners with cursor management
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <QApplication>

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
        : QObject(parent), m_parentWidget(parentWidget)
        , m_hadParentWidget(parentWidget != nullptr) {}

    ~AsyncRunnerBase() override;

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
        if (m_parentWidget) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            m_cursorOverrideActive = true;
        }
    }

    /**
     * Called at the end of run(). Clears running state and restores cursor.
     */
    void endRun()
    {
        m_running = false;
        // Restore on the push flag, not on parent liveness: the override
        // cursor stack is application-global, so a parent destroyed mid-run
        // must not strand the WaitCursor this runner pushed (#1695).
        if (m_cursorOverrideActive) {
            QApplication::restoreOverrideCursor();
            m_cursorOverrideActive = false;
        }
    }

    /** Guarded: dialogs die mid-task; a bare pointer dangles and the
     * completion lambdas build a QPointer from it (#1695). */
    QPointer<QWidget> m_parentWidget;
    /// True when a parent widget was provided at construction. A null-parent
    /// runner (no dialog to guard) still delivers results; only a parent
    /// that died mid-task suppresses the completion.
    bool m_hadParentWidget = false;
    bool m_running = false;
    bool m_cursorOverrideActive = false;
};

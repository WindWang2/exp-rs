// python_script_editor.h — Dockable Python script editor for SICNU GEO RS
// Provides a multi-line editor, output panel, and toolbar to run scripts
// against the embedded Python interpreter (QgisPython).
#pragma once

#include <QWidget>
#include <QThread>
#include <QString>

class QPlainTextEdit;
class QTextEdit;
class QPushButton;
class QLabel;
class QLineEdit;

namespace Sicnu {

/**
 * \brief Worker object that runs a Python script in a background thread.
 *
 * The worker lives in its own QThread so that long-running Python scripts
 * do not block the Qt GUI event loop.  QgisPython::runString() is invoked
 * from the worker thread; output produced by the interpreter is emitted
 * through QgisPython::outputReady() and is automatically queued back to
 * the GUI thread.
 */
class PythonScriptRunner : public QObject
{
    Q_OBJECT

public:
    explicit PythonScriptRunner(QObject *parent = nullptr) : QObject(parent) {}

public Q_SLOTS:
    /**
     * \brief Execute \a script in the embedded Python interpreter.
     *
     * Emits finished() when execution completes.  Errors reported by the
     * interpreter are passed back through the \a error string.
     */
    void runScript(const QString &script);

Q_SIGNALS:
    /**
     * \brief Emitted after runScript() finishes.
     * \param success true if no Python exception was raised.
     * \param error interpreter error message, empty on success.
     */
    void finished(bool success, const QString &error);
};

/**
 * \brief Multi-line Python script editor with output panel.
 *
 * The widget is intended to be embedded into a QgsDockWidget in the main
 * window.  It is guarded by the SICNU_EMBED_PYTHON macro and links against
 * the embedded Python runtime (QgisPython).
 *
 * Features:
 * - Multi-line code editor with monospace font.
 * - Run / Open / Save / Clear toolbar actions.
 * - Read-only output panel that captures stdout/stderr from Python.
 * - Background execution via PythonScriptRunner so the GUI stays responsive.
 * - Status message signal for integration with the main-window status bar.
 */
class PythonScriptEditor : public QWidget
{
    Q_OBJECT

public:
    explicit PythonScriptEditor(QWidget *parent = nullptr);
    ~PythonScriptEditor() override;

    /**
     * \brief Return the current script text.
     */
    QString script() const;

    /**
     * \brief Replace the current script text.
     */
    void setScript(const QString &script);

    /**
     * \brief Append text to the output panel.
     * \param text text to append.
     * \param isError if true, render in red.
     */
    void appendOutput(const QString &text, bool isError = false);

public Q_SLOTS:
    /**
     * \brief Run the current script in the background worker thread.
     */
    void runScript();

    /**
     * \brief Open a Python script from disk into the editor.
     */
    void openScript();

    /**
     * \brief Save the current script to disk.
     */
    void saveScript();

    /**
     * \brief Clear the output panel.
     */
    void clearOutput();

Q_SIGNALS:
    /**
     * \brief Emitted when a script finishes execution.
     * \param success true if execution succeeded.
     */
    void scriptExecuted(bool success);

    /**
     * \brief Emitted when the widget wants to show a transient status message.
     * \param message text suitable for a status bar.
     */
    void statusMessage(const QString &message);

private Q_SLOTS:
    void onPythonOutput(const QString &text);
    void onExecutionFinished(bool success, const QString &error);

private:
    void setupUi();
    void writeOutput(const QString &text);
    void writeError(const QString &text);

    QPlainTextEdit *m_editor = nullptr;
    QTextEdit *m_output = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    PythonScriptRunner *m_runner = nullptr;
    QThread *m_runnerThread = nullptr;

    QString m_currentFile;
    bool m_busy = false;
};

} // namespace Sicnu
